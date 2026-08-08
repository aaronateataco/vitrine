#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "diag.h"
#include "net.h"
#include "overrides.h"
#include "sgdb.h"
#include "vendor/cJSON.h"

#define SGDB_API "https://www.steamgriddb.com/api/v2"

static const char *json_string(const cJSON *node, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static int json_int(const cJSON *node, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

bool sgdb_find_game(const char *api_key, const char *name, int *out_game_id)
{
    if (!api_key || !api_key[0] || !name || !name[0] || !out_game_id)
        return false;

    char encoded[512];
    net_urlencode(name, encoded, sizeof(encoded));

    char url[768];
    snprintf(url, sizeof(url), SGDB_API "/search/autocomplete/%s", encoded);

    char *body = NULL;
    if (!net_get(url, api_key, &body, NULL))
        return false;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root)
        return false;

    bool found = false;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsArray(data)) {
        cJSON *first = cJSON_GetArrayItem(data, 0);
        if (first) {
            *out_game_id = json_int(first, "id");
            found = *out_game_id != 0;
            if (found)
                diag_logf("sgdb: \"%s\" -> game %d (%s)", name, *out_game_id,
                          json_string(first, "name"));
        }
    }

    cJSON_Delete(root);
    if (!found)
        diag_logf("sgdb: no match for \"%s\"", name);
    return found;
}

/* Preferred uploader first; ties broken by SteamGridDB's own score. */
static int compare_covers(const void *lhs, const void *rhs)
{
    const SgdbCover *a = lhs;
    const SgdbCover *b = rhs;

    if (a->preferred != b->preferred)
        return a->preferred ? -1 : 1;
    return b->score - a->score;
}

bool sgdb_list_covers(const char *api_key, int game_id, bool poster,
                      SgdbCoverList *out)
{
    if (!api_key || !api_key[0] || !out)
        return false;

    out->count = 0;

    char url[256];
    if (poster)
        snprintf(url, sizeof(url), SGDB_API "/grids/game/%d?dimensions=600x900", game_id);
    else
        snprintf(url, sizeof(url), SGDB_API "/icons/game/%d", game_id);

    char *body = NULL;
    if (!net_get(url, api_key, &body, NULL))
        return false;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root)
        return false;

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsArray(data)) {
        cJSON *node = NULL;
        cJSON_ArrayForEach(node, data) {
            if (out->count == SGDB_MAX_COVERS)
                break;

            const char *href = json_string(node, "url");
            if (!href[0])
                continue;

            SgdbCover *cover = &out->items[out->count];
            memset(cover, 0, sizeof(*cover));
            snprintf(cover->url, sizeof(cover->url), "%s", href);
            cover->id = json_int(node, "id");
            cover->score = json_int(node, "score");

            const cJSON *author = cJSON_GetObjectItemCaseSensitive(node, "author");
            if (cJSON_IsObject(author)) {
                snprintf(cover->author, sizeof(cover->author), "%s",
                         json_string(author, "steam64"));
                cover->preferred =
                    strcmp(cover->author, SGDB_PREFERRED_AUTHOR) == 0;
            }

            out->count++;
        }
    }

    cJSON_Delete(root);

    if (out->count > 1)
        qsort(out->items, out->count, sizeof(out->items[0]), compare_covers);

    diag_logf("sgdb: game %d -> %zu %s covers", game_id, out->count,
              poster ? "poster" : "icon");
    return out->count > 0;
}

/*
 * Cache filenames come from a hash of the entry key, which stays stable across
 * rescans and avoids having to sanitise arbitrary titles into filenames.
 */
void sgdb_cache_path(const Entry *entry, bool poster, const char *dir,
                     char *out, size_t out_size)
{
    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    unsigned long hash = 14695981039346656037UL;
    for (const char *p = key; *p; p++) {
        hash ^= (unsigned char)*p;
        hash *= 1099511628211UL;
    }

    snprintf(out, out_size, "%s/%016lx-%c.png", dir, hash, poster ? 'p' : 'i');
}

bool sgdb_cached(const Entry *entry, bool poster, const char *dir)
{
    char path[512];
    sgdb_cache_path(entry, poster, dir, path, sizeof(path));

    FILE *probe = fopen(path, "rb");
    if (!probe)
        return false;

    fclose(probe);
    return true;
}

bool sgdb_fetch_for_entry(const char *api_key, const Entry *entry, bool poster,
                          const char *dir, const char *locked_url,
                          char *out_url, size_t out_url_size)
{
    char path[512];
    sgdb_cache_path(entry, poster, dir, path, sizeof(path));
    mkdir(dir, 0777);

    /* A pinned choice skips the search entirely. */
    if (locked_url && locked_url[0]) {
        if (!net_download(locked_url, path))
            return false;
        if (out_url)
            snprintf(out_url, out_url_size, "%s", locked_url);
        return true;
    }

    int game_id = 0;
    if (!sgdb_find_game(api_key, entry->name, &game_id))
        return false;

    SgdbCoverList covers;
    if (!sgdb_list_covers(api_key, game_id, poster, &covers))
        return false;

    /* Sorted, so the first entry is the preferred uploader when they have one. */
    const SgdbCover *chosen = &covers.items[0];
    if (!net_download(chosen->url, path))
        return false;

    diag_logf("sgdb: cached %s for \"%s\"%s", path, entry->name,
              chosen->preferred ? " (preferred uploader)" : "");

    if (out_url)
        snprintf(out_url, out_url_size, "%s", chosen->url);
    return true;
}
