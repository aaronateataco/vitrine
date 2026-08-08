#pragma once

#include <switch.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    EntryKind_Homebrew = 0,
    EntryKind_Game     = 1,
    EntryKind_Title    = 2,
} EntryKind;

enum {
    ENTRY_NAME_LEN   = 0x200,
    ENTRY_AUTHOR_LEN = 0x100,
    ENTRY_PATH_LEN   = 0x301,
};

typedef struct {
    EntryKind kind;
    char      name[ENTRY_NAME_LEN];
    char      author[ENTRY_AUTHOR_LEN];
    char      path[ENTRY_PATH_LEN];  ///< NRO path (Homebrew) or ROM path (Game).
    u64       application_id;        ///< EntryKind_Title only.
    int       system_index;          ///< Index into SystemList; -1 when not a Game.

    /* Homebrew artwork, located during the scan and decoded lazily. */
    u64       icon_offset;           ///< Absolute byte offset of the JPEG in the NRO.
    u32       icon_size;             ///< 0 when the NRO carries no icon.
} Entry;

typedef struct {
    Entry  *items;
    size_t  count;
    size_t  capacity;
} EntryList;

/* entry.c */
bool   entry_list_init(EntryList *list);
void   entry_list_free(EntryList *list);
Entry *entry_list_add(EntryList *list);
void   entry_list_sort(EntryList *list);

/// Joins dir + name with a separator. False (and no write) if the result would not fit.
bool   path_join(char *dst, size_t dst_size, const char *dir, const char *name);

/* homebrew.c */
Result homebrew_scan(EntryList *list, const char *root);

/* titles.c */

/*
 * Called during long scans so the caller can repaint and keep the applet loop
 * alive. Without that the system treats the app as hung.
 */
typedef void (*ScanProgressFn)(void *ctx, size_t done, size_t total);

Result titles_scan(EntryList *list);
Result titles_scan_progress(EntryList *list, ScanProgressFn progress, void *ctx);

/*
 * Reads NACP name/author out of an NRO's appended asset blob. False when the
 * file is not an NRO or carries no metadata. Also used by diagnostics to
 * identify arbitrary NROs such as sdmc:/hbmenu.nro.
 */
bool   nro_read_metadata(const char *path, char *name, size_t name_size,
                         char *author, size_t author_size);

/*
 * Scans one directory of NROs, attributing them to `system_index` (-1 for
 * plain homebrew). Entries whose path is already present are skipped, so a
 * directory claimed by a system does not also appear under Homebrew.
 */
Result homebrew_scan_dir(EntryList *list, const char *dir, int system_index);
