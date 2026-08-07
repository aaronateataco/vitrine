#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "entry.h"

/* 'NRO0' and 'ASET' as little-endian u32. */
#define NRO_MAGIC  0x304F524E
#define ASET_MAGIC 0x54455341

/* Offsets into the NRO header, which itself follows a 0x10-byte start stub. */
#define NRO_MAGIC_OFFSET 0x10
#define NRO_SIZE_OFFSET  0x18

typedef struct {
    u64 offset;
    u64 size;
} AssetSection;

/* Appended directly after the NRO image; section offsets are relative to it. */
typedef struct {
    u32          magic;
    u32          version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
} AssetHeader;

static bool has_nro_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".nro") == 0;
}

/* Copies a fixed-width, possibly unterminated NACP field into a C string. */
static void copy_nacp_field(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t len = strnlen(src, src_size);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/*
 * Reads name/author out of the NRO's asset blob. Returns false for NROs built
 * without one, which is common enough that the caller must have a fallback.
 */
static bool read_nro_metadata(const char *path, Entry *entry)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    u32 magic = 0;
    u32 nro_size = 0;

    if (fseek(file, NRO_MAGIC_OFFSET, SEEK_SET) != 0 ||
        fread(&magic, sizeof(magic), 1, file) != 1 || magic != NRO_MAGIC ||
        fseek(file, NRO_SIZE_OFFSET, SEEK_SET) != 0 ||
        fread(&nro_size, sizeof(nro_size), 1, file) != 1 || nro_size == 0) {
        fclose(file);
        return false;
    }

    AssetHeader asset;
    if (fseek(file, nro_size, SEEK_SET) != 0 ||
        fread(&asset, sizeof(asset), 1, file) != 1 ||
        asset.magic != ASET_MAGIC) {
        fclose(file);
        return false;
    }

    /* Record where the icon lives; it is decoded only if it becomes visible. */
    if (asset.icon.size > 0 && asset.icon.size <= 0x100000) {
        entry->icon_offset = (u64)nro_size + asset.icon.offset;
        entry->icon_size = (u32)asset.icon.size;
    }

    if (asset.nacp.size < sizeof(NacpLanguageEntry)) {
        fclose(file);
        return false;
    }

    NacpLanguageEntry languages[16];
    size_t want = sizeof(languages);
    if (asset.nacp.size < want)
        want = asset.nacp.size;

    memset(languages, 0, sizeof(languages));
    if (fseek(file, (long)(nro_size + asset.nacp.offset), SEEK_SET) != 0 ||
        fread(languages, 1, want, file) != want) {
        fclose(file);
        return false;
    }
    fclose(file);

    /*
     * Homebrew NACPs are not filled in per-language consistently, and there is
     * no ns handle to ask for a preferred entry, so take the first populated one.
     */
    for (size_t i = 0; i < want / sizeof(NacpLanguageEntry); i++) {
        if (languages[i].name[0] == '\0')
            continue;

        copy_nacp_field(entry->name, sizeof(entry->name),
                        languages[i].name, sizeof(languages[i].name));
        copy_nacp_field(entry->author, sizeof(entry->author),
                        languages[i].author, sizeof(languages[i].author));
        return true;
    }

    return false;
}

static void add_nro(EntryList *list, const char *path)
{
    Entry *entry = entry_list_add(list);
    if (!entry)
        return;

    entry->kind = EntryKind_Homebrew;
    snprintf(entry->path, sizeof(entry->path), "%s", path);

    if (read_nro_metadata(path, entry))
        return;

    /* No asset blob: fall back to the bare filename. */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(entry->name, sizeof(entry->name), "%s", base);

    char *dot = strrchr(entry->name, '.');
    if (dot)
        *dot = '\0';
}

static void scan_subdirectory(EntryList *list, const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.' || !has_nro_extension(item->d_name))
            continue;

        char child[ENTRY_PATH_LEN];
        if (path_join(child, sizeof(child), path, item->d_name))
            add_nro(list, child);
    }

    closedir(dir);
}

/*
 * Follows the hbmenu layout: NROs sit either directly in /switch or one level
 * down in a per-app folder. Deeper nesting is not a convention, so it is ignored.
 */
Result homebrew_scan(EntryList *list, const char *root)
{
    DIR *dir = opendir(root);
    if (!dir)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.')
            continue;

        char path[ENTRY_PATH_LEN];
        if (!path_join(path, sizeof(path), root, item->d_name))
            continue;

        if (has_nro_extension(item->d_name))
            add_nro(list, path);
        else
            scan_subdirectory(list, path);
    }

    closedir(dir);
    return 0;
}
