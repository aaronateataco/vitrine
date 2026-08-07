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
Result titles_scan(EntryList *list);
