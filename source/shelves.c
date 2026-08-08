#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shelves.h"

#define SHELF_TITLES   "Installed Games"
#define SHELF_HOMEBREW "Homebrew"

bool shelves_init(ShelfList *shelves)
{
    memset(shelves, 0, sizeof(*shelves));
    shelves->capacity = 8;
    shelves->items = calloc(shelves->capacity, sizeof(*shelves->items));
    return shelves->items != NULL;
}

static void shelf_free(Shelf *shelf)
{
    free(shelf->items);
    shelf->items = NULL;
    shelf->count = 0;
    shelf->capacity = 0;
}

void shelves_free(ShelfList *shelves)
{
    for (size_t i = 0; i < shelves->count; i++)
        shelf_free(&shelves->items[i]);

    free(shelves->items);
    memset(shelves, 0, sizeof(*shelves));
}

static Shelf *shelf_add(ShelfList *shelves, const char *name)
{
    if (shelves->count == shelves->capacity) {
        size_t capacity = shelves->capacity * 2;
        Shelf *items = realloc(shelves->items, capacity * sizeof(*items));
        if (!items)
            return NULL;
        memset(items + shelves->capacity, 0,
               (capacity - shelves->capacity) * sizeof(*items));
        shelves->items = items;
        shelves->capacity = capacity;
    }

    Shelf *shelf = &shelves->items[shelves->count++];
    memset(shelf, 0, sizeof(*shelf));
    snprintf(shelf->name, sizeof(shelf->name), "%s", name);
    return shelf;
}

static bool shelf_push(Shelf *shelf, size_t entry_index)
{
    if (shelf->count == shelf->capacity) {
        size_t capacity = shelf->capacity ? shelf->capacity * 2 : 16;
        size_t *items = realloc(shelf->items, capacity * sizeof(*items));
        if (!items)
            return false;
        shelf->items = items;
        shelf->capacity = capacity;
    }

    shelf->items[shelf->count++] = entry_index;
    return true;
}

static Shelf *shelf_find(ShelfList *shelves, const char *name)
{
    for (size_t i = 0; i < shelves->count; i++)
        if (strcmp(shelves->items[i].name, name) == 0)
            return &shelves->items[i];
    return NULL;
}

/* Remembered so a rescan does not throw away where the user was. */
typedef struct {
    char   name[SHELF_NAME_LEN];
    size_t cursor;
} SavedCursor;

void shelves_build(ShelfList *shelves, const EntryList *list, const SystemList *systems,
                   const OverrideList *overrides, bool show_hidden)
{
    size_t saved_count = shelves->count;
    SavedCursor *saved = NULL;

    if (saved_count) {
        saved = calloc(saved_count, sizeof(*saved));
        if (saved) {
            for (size_t i = 0; i < saved_count; i++) {
                snprintf(saved[i].name, sizeof(saved[i].name), "%s",
                         shelves->items[i].name);
                saved[i].cursor = shelves->items[i].cursor;
            }
        }
    }

    for (size_t i = 0; i < shelves->count; i++)
        shelf_free(&shelves->items[i]);
    shelves->count = 0;

    /* Fixed leading order, then the user's own systems.ini ordering. */
    shelf_add(shelves, SHELF_TITLES);
    shelf_add(shelves, SHELF_HOMEBREW);
    for (size_t i = 0; i < systems->count; i++)
        if (!shelf_find(shelves, systems->items[i].name))
            shelf_add(shelves, systems->items[i].name);

    for (size_t i = 0; i < list->count; i++) {
        const Entry *entry = &list->items[i];
        Shelf *shelf = NULL;

        if (overrides && !show_hidden && overrides_hidden(overrides, entry))
            continue;

        switch (entry->kind) {
            case EntryKind_Title:
                shelf = shelf_find(shelves, SHELF_TITLES);
                break;
            case EntryKind_Homebrew:
                /* Re-tagged homebrew files sit with the installed games. */
                shelf = shelf_find(shelves,
                                   overrides && overrides_promoted(overrides, entry)
                                       ? SHELF_TITLES : SHELF_HOMEBREW);
                break;
            case EntryKind_Game:
                if (entry->system_index >= 0 &&
                    (size_t)entry->system_index < systems->count)
                    shelf = shelf_find(shelves, systems->items[entry->system_index].name);
                break;
        }

        if (shelf)
            shelf_push(shelf, i);
    }

    /* Drop empties in place, preserving relative order. */
    size_t out = 0;
    for (size_t i = 0; i < shelves->count; i++) {
        if (shelves->items[i].count == 0) {
            shelf_free(&shelves->items[i]);
            continue;
        }
        if (out != i)
            shelves->items[out] = shelves->items[i];
        out++;
    }
    shelves->count = out;

    for (size_t i = 0; i < shelves->count; i++) {
        Shelf *shelf = &shelves->items[i];

        if (saved) {
            for (size_t j = 0; j < saved_count; j++) {
                if (strcmp(saved[j].name, shelf->name) == 0) {
                    shelf->cursor = saved[j].cursor;
                    break;
                }
            }
        }

        if (shelf->cursor >= shelf->count)
            shelf->cursor = shelf->count - 1;
    }

    free(saved);
}
