#pragma once

#include "entry.h"

enum {
    SYSTEM_NAME_LEN = 64,
    SYSTEM_ARGS_LEN = 256,
    SYSTEM_EXTS_LEN = 256,
    SYSTEM_MAX_ROMS = 6,
};

/// One emulated platform: which core NRO runs it, and where its ROMs live.
typedef struct {
    char   name[SYSTEM_NAME_LEN];
    char   core[ENTRY_PATH_LEN];        ///< Path to the core NRO.
    char   roms[SYSTEM_MAX_ROMS][ENTRY_PATH_LEN]; ///< Directories to scan.
    size_t roms_count;
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
bool   system_expand_args(const System *system, const char *rom, char *out, size_t out_size);
