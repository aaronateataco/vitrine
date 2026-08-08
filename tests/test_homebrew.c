/*
 * Host-side checks for the NRO asset-header parsing in homebrew.c.
 *
 * Builds a synthetic NRO matching the real on-disk layout, so the offset
 * arithmetic can be exercised without a Switch. Verified to agree with a real
 * devkitPro-produced NRO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "entry.h"

#define NRO_IMAGE_SIZE 0x1000
#define NACP_OFFSET    0x100
#define TEST_DIR       "testdata-homebrew"

typedef struct { u64 offset; u64 size; } AssetSection;
typedef struct {
    u32 magic; u32 version;
    AssetSection icon, nacp, romfs;
} AssetHeader;

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void write_nro(const char *path, int with_asset,
                      const char *name, const char *author, int lang_index)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }

    u8 *image = calloc(1, NRO_IMAGE_SIZE);
    u32 magic = 0x304F524E;              /* 'NRO0' */
    u32 size  = NRO_IMAGE_SIZE;
    memcpy(image + 0x10, &magic, 4);
    memcpy(image + 0x18, &size, 4);
    fwrite(image, 1, NRO_IMAGE_SIZE, f);
    free(image);

    if (with_asset) {
        AssetHeader header;
        memset(&header, 0, sizeof(header));
        header.magic = 0x54455341;       /* 'ASET' */
        header.nacp.offset = NACP_OFFSET;
        header.nacp.size = sizeof(NacpLanguageEntry) * 16;
        fwrite(&header, sizeof(header), 1, f);

        u8 pad[NACP_OFFSET];
        memset(pad, 0, sizeof(pad));
        fwrite(pad, 1, NACP_OFFSET - sizeof(header), f);

        NacpLanguageEntry langs[16];
        memset(langs, 0, sizeof(langs));
        /* Fixed-width and deliberately not NUL-padded, as real NACPs are. */
        memcpy(langs[lang_index].name, name, strlen(name));
        memcpy(langs[lang_index].author, author, strlen(author));
        fwrite(langs, sizeof(langs), 1, f);
    }

    fclose(f);
}

static const Entry *find(const EntryList *list, const char *name)
{
    for (size_t i = 0; i < list->count; i++)
        if (strcmp(list->items[i].name, name) == 0)
            return &list->items[i];
    return NULL;
}

int main(void)
{
    if (system("rm -rf " TEST_DIR " && mkdir -p " TEST_DIR "/nested") != 0)
        return 1;

    write_nro(TEST_DIR "/alpha.nro", 1, "Alpha App", "Alpha Author", 0);
    write_nro(TEST_DIR "/nested/beta.nro", 1, "Beta App", "Beta Author", 3);
    write_nro(TEST_DIR "/gamma_nometa.nro", 0, "", "", 0);

    FILE *junk = fopen(TEST_DIR "/notes.txt", "w");
    if (junk) { fputs("ignore me", junk); fclose(junk); }

    EntryList list;
    if (!entry_list_init(&list)) return 1;

    Result rc = homebrew_scan(&list, TEST_DIR);
    check("scan succeeds", R_SUCCEEDED(rc));
    check("finds exactly 3 NROs", list.count == 3);

    const Entry *alpha = find(&list, "Alpha App");
    check("top-level NRO name from NACP", alpha != NULL);
    if (alpha) {
        check("top-level NRO author from NACP", strcmp(alpha->author, "Alpha Author") == 0);
        check("name is NUL-terminated, not padded", strlen(alpha->name) == 9);
        check("kind is homebrew", alpha->kind == EntryKind_Homebrew);
    }

    const Entry *beta = find(&list, "Beta App");
    check("nested NRO found one level down", beta != NULL);
    if (beta) {
        check("falls through to populated language slot",
              strcmp(beta->author, "Beta Author") == 0);
        check("nested path recorded", strstr(beta->path, "nested/beta.nro") != NULL);
    }

    check("no-metadata NRO falls back to filename", find(&list, "gamma_nometa") != NULL);

    /* The public reader is what diagnostics uses to identify e.g. hbmenu.nro. */
    char name[0x200] = { 0 };
    char author[0x100] = { 0 };
    check("public NRO reader returns metadata",
          nro_read_metadata(TEST_DIR "/alpha.nro", name, sizeof(name),
                            author, sizeof(author)));
    check("public reader name matches", strcmp(name, "Alpha App") == 0);
    check("public reader author matches", strcmp(author, "Alpha Author") == 0);
    check("public reader rejects a non-NRO",
          !nro_read_metadata(TEST_DIR "/notes.txt", name, sizeof(name),
                             author, sizeof(author)));
    check("public reader rejects a missing file",
          !nro_read_metadata(TEST_DIR "/nope.nro", name, sizeof(name),
                             author, sizeof(author)));
    check("non-NRO file ignored", find(&list, "notes") == NULL);

    entry_list_free(&list);
    if (system("rm -rf " TEST_DIR) != 0) { /* best effort */ }

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
