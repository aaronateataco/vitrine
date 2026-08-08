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
    bool hidden;    ///< Omitted from every shelf unless "show hidden" is on.
    bool promote;   ///< Homebrew that should sit with the installed games.
} Override;

/// Global display preferences, persisted alongside the per-entry overrides.
typedef struct {
    bool show_hidden;    ///< Reveal hidden entries so they can be restored.
    bool poster_tiles;   ///< 2:3 box art instead of 1:1 Switch-style icons.
    bool large_tiles;    ///< Fewer, bigger tiles per shelf.
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

/// Clears every hidden flag; the promote flags are left alone.
void   overrides_unhide_all(OverrideList *overrides);
