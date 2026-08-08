#pragma once

#include "entry.h"

enum {
    SYSTEM_NAME_LEN  = 64,
    SYSTEM_ARGS_LEN  = 256,
    SYSTEM_EXTS_LEN  = 256,
    SYSTEM_MAX_ROMS  = 6,
    SYSTEM_MAX_CORES = 4,
};

/// One emulated platform: which core NRO runs it, and where its ROMs live.
typedef struct {
    char   name[SYSTEM_NAME_LEN];
    /*
     * Candidates in preference order; the first that exists is used. This is
     * what lets VITRINE prefer its own cores while still falling back to a
     * tico install, rather than depending on one.
     */
    char   core[SYSTEM_MAX_CORES][ENTRY_PATH_LEN];
    size_t core_count;
    char   roms[SYSTEM_MAX_ROMS][ENTRY_PATH_LEN]; ///< Directories to scan.
    size_t roms_count;
    /*
     * Directories of standalone NROs that belong on this shelf rather than in
     * Homebrew. Launched directly; no core is involved.
     */
    char   nro[SYSTEM_MAX_ROMS][ENTRY_PATH_LEN];
    size_t nro_count;
    char   extensions[SYSTEM_EXTS_LEN]; ///< Comma-separated, without dots.
    char   args[SYSTEM_ARGS_LEN];       ///< Template using {core} and {rom}.
} System;

typedef struct SystemList {
    System *items;
    size_t  count;
    size_t  capacity;
} SystemList;

bool   systems_init(SystemList *systems);
void   systems_free(SystemList *systems);

/// Parses the INI at `path`. Returns LibnxError_NotFound if it does not exist.
Result systems_load(SystemList *systems, const char *path);

/// Writes a commented starter config, so a fresh install has something to edit.
Result systems_write_example(const char *path);

bool   system_matches(const System *system, const char *filename);

/// First core candidate present on disk, or NULL if none of them are.
const char *system_pick_core(const System *system);

bool   system_expand_args(const System *system, const char *core, const char *rom,
                          char *out, size_t out_size);
