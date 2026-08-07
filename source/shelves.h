#pragma once

#include "config.h"

enum { SHELF_NAME_LEN = 64 };

/*
 * One horizontal row of the library: a platform, plus the homebrew and
 * installed-title groups. Cursor and scroll live here so moving between shelves
 * returns you to where you were, rather than resetting to the start.
 */
typedef struct {
    char    name[SHELF_NAME_LEN];
    size_t *items;        ///< Indices into the EntryList.
    size_t  count;
    size_t  capacity;
    size_t  cursor;       ///< Selected column within this shelf.
    float   scroll_x;     ///< Animated, in pixels.
} Shelf;

typedef struct {
    Shelf *items;
    size_t count;
    size_t capacity;
} ShelfList;

bool shelves_init(ShelfList *shelves);
void shelves_free(ShelfList *shelves);

/*
 * Rebuilds from the entry list. Shelf order is deliberate: installed games,
 * then homebrew, then ROM platforms in the order systems.ini declares them,
 * since that ordering is the user's own. Empty shelves are dropped.
 * Cursors survive a rebuild where the shelf name is unchanged.
 */
void shelves_build(ShelfList *shelves, const EntryList *list, const SystemList *systems);
