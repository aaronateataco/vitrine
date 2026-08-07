#include <dirent.h>
#include <string.h>

#include "roms.h"

/* Deep enough for the usual roms/<system>/<letter>/ arrangements. */
#define MAX_DEPTH 4

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void add_rom(EntryList *list, const System *system, int system_index,
                    const char *path, const char *filename)
{
    Entry *entry = entry_list_add(list);
    if (!entry)
        return;

    entry->kind = EntryKind_Game;
    entry->system_index = system_index;
    copy_str(entry->path, sizeof(entry->path), path);

    /* Display the bare filename; the system name rides in the author column. */
    copy_str(entry->name, sizeof(entry->name), filename);
    char *dot = strrchr(entry->name, '.');
    if (dot)
        *dot = '\0';

    copy_str(entry->author, sizeof(entry->author), system->name);
}

static void scan_directory(EntryList *list, const System *system, int system_index,
                           const char *directory, int depth)
{
    if (depth > MAX_DEPTH)
        return;

    DIR *dir = opendir(directory);
    if (!dir)
        return;

    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.')
            continue;

        char path[ENTRY_PATH_LEN];
        if (!path_join(path, sizeof(path), directory, item->d_name))
            continue;

        if (system_matches(system, item->d_name))
            add_rom(list, system, system_index, path, item->d_name);
        else
            scan_directory(list, system, system_index, path, depth + 1);
    }

    closedir(dir);
}

Result roms_scan(EntryList *list, const SystemList *systems)
{
    for (size_t i = 0; i < systems->count; i++) {
        const System *system = &systems->items[i];

        if (system->extensions[0] == '\0')
            continue;

        /* Missing directories just yield nothing, so listing a tico path that
           is not present on this card costs one failed opendir. */
        for (size_t r = 0; r < system->roms_count; r++)
            scan_directory(list, system, (int)i, system->roms[r], 0);
    }

    return 0;
}
