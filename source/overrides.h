#pragma once

#include "entry.h"

/*
 * Per-entry user decisions, persisted to config.json.
 *
 * Hiding and re-tagging are the same mechanism with two flags, keyed by an
 * identity that survives a rescan: the file path for homebrew and ROMs, and the
 * application id for installed titles (whose paths are not user-visible).
 */
typedef struct {
    char key[ENTRY_PATH_LEN];
    bool hidden;         ///< Omitted from every shelf unless "show hidden" is on.
    bool promote;        ///< Homebrew that should sit with the installed games.
    /* Pinned SteamGridDB artwork, one slot per shape - a square icon and a 2:3
       poster are different images and must not overwrite each other. */
    char cover_icon[512];
    char cover_poster[512];
} Override;

/// Global display preferences, persisted alongside the per-entry overrides.
typedef struct {
    bool show_hidden;    ///< Reveal hidden entries so they can be restored.
    bool poster_tiles;   ///< 2:3 box art instead of 1:1 Switch-style icons.
    int  cover_size;     ///< 0 standard, 1 large, 2 extra large.
    int  theme;          ///< Index into the UI theme table.
    int  layout;         ///< 0 shelves, 1 console-style carousel.
    char sgdb_key[128];  ///< SteamGridDB API key, entered via the system keyboard.
    char ra_user[64];    ///< RetroAchievements username.
    char ra_key[128];    ///< RetroAchievements web API key.
} Prefs;

typedef struct {
    Prefs     prefs;
    Override *items;
    size_t    count;
    size_t    capacity;
    bool      dirty;   ///< Set by the toggles; cleared by a successful save.
} OverrideList;

bool   overrides_init(OverrideList *overrides);
void   overrides_free(OverrideList *overrides);

/// Missing file is not an error: it simply means nothing has been customised.
Result overrides_load(OverrideList *overrides, const char *path);
Result overrides_save(OverrideList *overrides, const char *path);

void   overrides_key(const Entry *entry, char *out, size_t out_size);

const Override *overrides_find(const OverrideList *overrides, const char *key);

bool   overrides_hidden(const OverrideList *overrides, const Entry *entry);
bool   overrides_promoted(const OverrideList *overrides, const Entry *entry);

/// Both toggles create the record on demand and mark the list dirty.
void   overrides_toggle_hidden(OverrideList *overrides, const Entry *entry);
void   overrides_toggle_promote(OverrideList *overrides, const Entry *entry);

/// Pins a specific cover for an entry, so re-fetching will not change it.
void   overrides_set_cover(OverrideList *overrides, const Entry *entry,
                           bool poster, const char *url);

/// Empty string when nothing has been pinned for that shape.
const char *overrides_cover(const OverrideList *overrides, const Entry *entry,
                            bool poster);

/// Clears every hidden flag; the promote flags are left alone.
void   overrides_unhide_all(OverrideList *overrides);

/*
 * Whole shelves can be hidden too (e.g. Homebrew). They share the override
 * table under a "shelf:" key prefix, so one save path covers everything.
 */
bool   overrides_shelf_hidden(const OverrideList *overrides, const char *shelf_name);
void   overrides_toggle_shelf(OverrideList *overrides, const char *shelf_name);
