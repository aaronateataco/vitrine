#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "render.h"

#define TEXT_CACHE_SLOTS 96
#define TEXT_KEY_LEN     128

typedef struct {
    char         key[TEXT_KEY_LEN];
    TTF_Font    *font;
    SDL_Texture *texture;
    int          w, h;
    bool         used;
} TextEntry;

typedef struct {
    TextEntry slots[TEXT_CACHE_SLOTS];
    size_t    count;
} TextCache;

/*
 * The system font lives in pl's shared memory. libnx hands back a pointer and a
 * length with no transformation, and FreeType consumes it directly - so nothing
 * needs decoding and no font has to be shipped with the app. The data is never
 * copied off the console.
 */
static bool load_shared_font(Render *render)
{
    static PlFontData font_data;

    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc))
        return false;

    rc = plGetSharedFontByType(&font_data, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        plExit();
        return false;
    }

    /* SDL takes ownership of neither RWops target; the memory is pl's. */
    SDL_RWops *body = SDL_RWFromConstMem(font_data.address, (int)font_data.size);
    SDL_RWops *head = SDL_RWFromConstMem(font_data.address, (int)font_data.size);
    if (!body || !head) {
        plExit();
        return false;
    }

    render->font = TTF_OpenFontRW(body, 1, 22);
    render->font_large = TTF_OpenFontRW(head, 1, 30);

    return render->font != NULL && render->font_large != NULL;
}

bool render_init(Render *render)
{
    memset(render, 0, sizeof(*render));

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;
    if (TTF_Init() != 0)
        return false;

    render->window = SDL_CreateWindow("LUDI-NX", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    if (!render->window)
        return false;

    render->renderer = SDL_CreateRenderer(render->window, -1,
                                          SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!render->renderer)
        return false;

    SDL_SetRenderDrawBlendMode(render->renderer, SDL_BLENDMODE_BLEND);

    if (!load_shared_font(render))
        return false;

    render->text_cache = calloc(1, sizeof(TextCache));
    return render->text_cache != NULL;
}

void render_exit(Render *render)
{
    TextCache *cache = render->text_cache;
    if (cache) {
        for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++)
            if (cache->slots[i].used && cache->slots[i].texture)
                SDL_DestroyTexture(cache->slots[i].texture);
        free(cache);
    }

    if (render->font)       TTF_CloseFont(render->font);
    if (render->font_large) TTF_CloseFont(render->font_large);
    if (render->renderer)   SDL_DestroyRenderer(render->renderer);
    if (render->window)     SDL_DestroyWindow(render->window);

    TTF_Quit();
    SDL_Quit();
    plExit();
}

/*
 * Rasterising every visible label each frame is far too slow at 60Hz, so
 * results are memoised. The table is small and simply flushed when full, which
 * is fine because the working set is one screenful of labels.
 */
static TextEntry *cache_lookup(TextCache *cache, TTF_Font *font, const char *text)
{
    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        TextEntry *slot = &cache->slots[i];
        if (slot->used && slot->font == font && strcmp(slot->key, text) == 0)
            return slot;
    }
    return NULL;
}

static void cache_flush(TextCache *cache)
{
    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        if (cache->slots[i].used && cache->slots[i].texture)
            SDL_DestroyTexture(cache->slots[i].texture);
        memset(&cache->slots[i], 0, sizeof(cache->slots[i]));
    }
    cache->count = 0;
}

static TextEntry *cache_insert(Render *render, TTF_Font *font, const char *text)
{
    TextCache *cache = render->text_cache;

    if (cache->count == TEXT_CACHE_SLOTS)
        cache_flush(cache);

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, white);
    if (!surface)
        return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(render->renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);
    if (!texture)
        return NULL;

    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        TextEntry *slot = &cache->slots[i];
        if (slot->used)
            continue;

        snprintf(slot->key, sizeof(slot->key), "%s", text);
        slot->font = font;
        slot->texture = texture;
        slot->w = w;
        slot->h = h;
        slot->used = true;
        cache->count++;
        return slot;
    }

    SDL_DestroyTexture(texture);
    return NULL;
}

/* Textures are cached white and tinted on draw, so one raster serves any colour. */
static TextEntry *text_entry(Render *render, TTF_Font *font, const char *text)
{
    if (!text || !text[0])
        return NULL;

    TextEntry *slot = cache_lookup(render->text_cache, font, text);
    return slot ? slot : cache_insert(render, font, text);
}

void render_text(Render *render, TTF_Font *font, int x, int y,
                 SDL_Color color, const char *text)
{
    TextEntry *slot = text_entry(render, font, text);
    if (!slot)
        return;

    SDL_SetTextureColorMod(slot->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(slot->texture, color.a);

    SDL_Rect dst = { x, y, slot->w, slot->h };
    SDL_RenderCopy(render->renderer, slot->texture, NULL, &dst);
}

void render_text_fit(Render *render, TTF_Font *font, int x, int y, int width,
                     SDL_Color color, const char *text)
{
    if (!text || !text[0])
        return;

    TextEntry *slot = text_entry(render, font, text);
    if (!slot)
        return;

    SDL_SetTextureColorMod(slot->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(slot->texture, color.a);

    if (slot->w <= width) {
        SDL_Rect dst = { x + (width - slot->w) / 2, y, slot->w, slot->h };
        SDL_RenderCopy(render->renderer, slot->texture, NULL, &dst);
        return;
    }

    /* Too wide: show the leading portion rather than squashing the glyphs. */
    SDL_Rect src = { 0, 0, width, slot->h };
    SDL_Rect dst = { x, y, width, slot->h };
    SDL_RenderCopy(render->renderer, slot->texture, &src, &dst);
}

void render_fill(Render *render, SDL_Rect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(render->renderer, &rect);
}

void render_outline(Render *render, SDL_Rect rect, int thickness, SDL_Color color)
{
    SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; i < thickness; i++) {
        SDL_Rect r = { rect.x - i, rect.y - i, rect.w + 2 * i, rect.h + 2 * i };
        SDL_RenderDrawRect(render->renderer, &r);
    }
}

void render_shadow(Render *render, SDL_Rect rect, int spread)
{
    for (int i = spread; i > 0; i--) {
        /* Quadratic falloff reads as a soft shadow rather than a hard band. */
        float t = (float)i / (float)spread;
        Uint8 alpha = (Uint8)(70.0f * (1.0f - t) * (1.0f - t));

        SDL_SetRenderDrawColor(render->renderer, 0, 0, 0, alpha);
        SDL_Rect r = { rect.x - i, rect.y - i + 2, rect.w + 2 * i, rect.h + 2 * i };
        SDL_RenderDrawRect(render->renderer, &r);
    }
}

void render_edge_fade(Render *render, SDL_Rect rect, SDL_Color color, bool from_left)
{
    for (int x = 0; x < rect.w; x++) {
        float t = (float)x / (float)rect.w;
        if (!from_left)
            t = 1.0f - t;

        SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b,
                               (Uint8)(color.a * (1.0f - t)));
        SDL_RenderDrawLine(render->renderer, rect.x + x, rect.y,
                           rect.x + x, rect.y + rect.h);
    }
}
