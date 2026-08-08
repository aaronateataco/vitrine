#pragma once

#include "entry.h"

/*
 * SteamGridDB client.
 *
 * Covers are fetched per entry and cached on the SD card, keyed by a hash of
 * the entry's identity so a rescan reuses them. Nothing is bundled with the
 * app; artwork is downloaded by the user with their own API key.
 */

/// Uploader whose covers are preferred over all others when present.
#define SGDB_PREFERRED_AUTHOR "76561199237351291"

enum { SGDB_URL_LEN = 512, SGDB_MAX_COVERS = 24 };

typedef struct {
    char url[SGDB_URL_LEN];
    char author[64];      ///< steam64 id of the uploader.
    int  id;
    int  score;
    bool preferred;       ///< From SGDB_PREFERRED_AUTHOR.
} SgdbCover;

typedef struct {
    SgdbCover items[SGDB_MAX_COVERS];
    size_t    count;
} SgdbCoverList;

/// Resolves a display name to a SteamGridDB game id.
bool sgdb_find_game(const char *api_key, const char *name, int *out_game_id);

/*
 * Lists candidate covers, preferred uploader first and higher scores next.
 * `poster` picks 600x900 grids; otherwise square icons are requested.
 */
bool sgdb_list_covers(const char *api_key, int game_id, bool poster,
                      SgdbCoverList *out);

/// Stable cache path for an entry, independent of its position in the library.
void sgdb_cache_path(const Entry *entry, bool poster, const char *dir,
                     char *out, size_t out_size);

/// True when a cover is already cached for this entry.
bool sgdb_cached(const Entry *entry, bool poster, const char *dir);

/*
 * Full pipeline for one entry: search, choose, download, cache. `locked_url`
 * bypasses the search when the user has pinned a specific cover.
 */
bool sgdb_fetch_for_entry(const char *api_key, const Entry *entry, bool poster,
                          const char *dir, const char *locked_url,
                          char *out_url, size_t out_url_size);
