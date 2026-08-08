#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_image.h>
#include <switch.h>

#include "icons.h"
#include "sgdb.h"

/* One screenful plus a margin, so scrolling does not thrash the cache. */
#define ICON_SLOTS 48

static void flush(IconCache *cache);

typedef struct {
    size_t       index;
    SDL_Texture *texture;
    bool         used;
    bool         attempted;   ///< Remembered so failures are not retried each frame.
} IconSlot;

struct IconCache {
    SDL_Renderer             *renderer;
    char                      covers_dir[256];
    IconSlot                  slots[ICON_SLOTS];
    size_t                    count;
    NsApplicationControlData *control;   ///< ~0x24000; allocated once, reused.
    bool                      ns_ready;
};

IconCache *icons_create(SDL_Renderer *renderer, const char *covers_dir)
{
    IconCache *cache = calloc(1, sizeof(*cache));
    if (!cache)
        return NULL;

    cache->renderer = renderer;
    snprintf(cache->covers_dir, sizeof(cache->covers_dir), "%s",
             covers_dir ? covers_dir : "");

    /* Icons are JPEG on both paths. */
    IMG_Init(IMG_INIT_JPG);

    /* libnx service handles are refcounted, so this nests safely with titles.c. */
    cache->ns_ready = R_SUCCEEDED(nsInitialize());
    if (cache->ns_ready)
        cache->control = malloc(sizeof(*cache->control));

    return cache;
}

void icons_destroy(IconCache *cache)
{
    if (!cache)
        return;

    for (size_t i = 0; i < ICON_SLOTS; i++)
        if (cache->slots[i].texture)
            SDL_DestroyTexture(cache->slots[i].texture);

    free(cache->control);
    if (cache->ns_ready)
        nsExit();

    IMG_Quit();
    free(cache);
}

static SDL_Texture *texture_from_memory(IconCache *cache, const void *data, size_t size)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (!rw)
        return NULL;

    /* Frees the RWops regardless of outcome. */
    SDL_Surface *surface = IMG_Load_RW(rw, 1);
    if (!surface)
        return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(cache->renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

/* Re-reads the JPEG recorded during scanning rather than holding every icon in RAM. */
static SDL_Texture *load_homebrew_icon(IconCache *cache, const Entry *entry)
{
    if (entry->icon_size == 0 || entry->icon_size > 0x100000)
        return NULL;

    FILE *file = fopen(entry->path, "rb");
    if (!file)
        return NULL;

    void *buffer = malloc(entry->icon_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    SDL_Texture *texture = NULL;
    if (fseek(file, (long)entry->icon_offset, SEEK_SET) == 0 &&
        fread(buffer, 1, entry->icon_size, file) == entry->icon_size)
        texture = texture_from_memory(cache, buffer, entry->icon_size);

    free(buffer);
    fclose(file);
    return texture;
}

static SDL_Texture *load_title_icon(IconCache *cache, const Entry *entry)
{
    if (!cache->ns_ready || !cache->control)
        return NULL;

    u64 actual = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                            entry->application_id, cache->control,
                                            sizeof(*cache->control), &actual);
    if (R_FAILED(rc) || actual <= sizeof(cache->control->nacp))
        return NULL;

    size_t icon_size = (size_t)(actual - sizeof(cache->control->nacp));
    return texture_from_memory(cache, cache->control->icon, icon_size);
}

static IconSlot *find_slot(IconCache *cache, size_t index)
{
    for (size_t i = 0; i < ICON_SLOTS; i++)
        if (cache->slots[i].used && cache->slots[i].index == index)
            return &cache->slots[i];
    return NULL;
}

void icons_flush(IconCache *cache)
{
    if (cache)
        flush(cache);
}

static void flush(IconCache *cache)
{
    for (size_t i = 0; i < ICON_SLOTS; i++) {
        if (cache->slots[i].texture)
            SDL_DestroyTexture(cache->slots[i].texture);
        memset(&cache->slots[i], 0, sizeof(cache->slots[i]));
    }
    cache->count = 0;
}

/* A downloaded cover wins over the console's own icon when one exists. */
static SDL_Texture *load_cached_cover(IconCache *cache, const Entry *entry, bool poster)
{
    if (!cache->covers_dir[0])
        return NULL;

    char path[512];
    sgdb_cache_path(entry, poster, cache->covers_dir, path, sizeof(path));

    FILE *probe = fopen(path, "rb");
    if (!probe)
        return NULL;
    fclose(probe);

    return IMG_LoadTexture(cache->renderer, path);
}

SDL_Texture *icons_get(IconCache *cache, const Entry *entry, size_t index,
                       bool poster)
{
    IconSlot *slot = find_slot(cache, index);
    if (slot)
        return slot->texture;   /* NULL here means "already tried and failed". */

    if (cache->count == ICON_SLOTS)
        flush(cache);

    SDL_Texture *texture = load_cached_cover(cache, entry, poster);
    if (texture)
        goto store;

    switch (entry->kind) {
        case EntryKind_Homebrew: texture = load_homebrew_icon(cache, entry); break;
        case EntryKind_Title:    texture = load_title_icon(cache, entry);    break;
        default:                 texture = NULL;                             break;
    }

store:
    for (size_t i = 0; i < ICON_SLOTS; i++) {
        if (cache->slots[i].used)
            continue;
        cache->slots[i].index = index;
        cache->slots[i].texture = texture;
        cache->slots[i].used = true;
        cache->slots[i].attempted = true;
        cache->count++;
        break;
    }

    return texture;
}
