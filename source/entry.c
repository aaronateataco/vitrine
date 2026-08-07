#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "entry.h"

bool entry_list_init(EntryList *list)
{
    memset(list, 0, sizeof(*list));
    list->capacity = 64;
    list->items = calloc(list->capacity, sizeof(*list->items));
    return list->items != NULL;
}

void entry_list_free(EntryList *list)
{
    free(list->items);
    memset(list, 0, sizeof(*list));
}

Entry *entry_list_add(EntryList *list)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity * 2;
        Entry *items = realloc(list->items, capacity * sizeof(*items));
        if (!items)
            return NULL;
        list->items = items;
        list->capacity = capacity;
    }

    Entry *entry = &list->items[list->count++];
    memset(entry, 0, sizeof(*entry));
    entry->system_index = -1;
    return entry;
}

bool path_join(char *dst, size_t dst_size, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);

    /* Drop a trailing separator so "sdmc:/roms/" and "sdmc:/roms" behave alike. */
    while (dir_len > 0 && dir[dir_len - 1] == '/')
        dir_len--;

    if (dir_len + 1 + name_len + 1 > dst_size)
        return false;

    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, name, name_len);
    dst[dir_len + 1 + name_len] = '\0';
    return true;
}

static int compare_entries(const void *lhs, const void *rhs)
{
    const Entry *a = lhs;
    const Entry *b = rhs;

    /* Homebrew first, then installed titles; alphabetical within each group. */
    if (a->kind != b->kind)
        return (int)a->kind - (int)b->kind;

    return strcasecmp(a->name, b->name);
}

void entry_list_sort(EntryList *list)
{
    if (list->count > 1)
        qsort(list->items, list->count, sizeof(*list->items), compare_entries);
}
