/* Host-side checks for the INI parser, extension matching and ROM scanning. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "roms.h"

#define TEST_DIR "testdata-config"

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void touch(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs("x", f); fclose(f); }
}

int main(void)
{
    if (system("rm -rf " TEST_DIR
               " && mkdir -p " TEST_DIR "/roms/gba/sub"
               " && mkdir -p " TEST_DIR "/roms/n64"
               " && mkdir -p " TEST_DIR "/tico/roms/gba") != 0)
        return 1;

    touch(TEST_DIR "/roms/gba/Zelda.gba");
    touch(TEST_DIR "/roms/gba/Metroid.AGB");
    touch(TEST_DIR "/roms/gba/sub/Deep.gba");
    touch(TEST_DIR "/roms/gba/notes.txt");
    touch(TEST_DIR "/roms/gba/cover.gbc");
    touch(TEST_DIR "/roms/gba/README");
    touch(TEST_DIR "/roms/n64/Mario.z64");
    touch(TEST_DIR "/tico/roms/gba/FromTico.gba");

    FILE *f = fopen(TEST_DIR "/systems.ini", "w");
    if (!f) return 1;
    fputs("# a comment\n"
          "; another comment\n"
          "\n"
          "[Game Boy Advance]\n"
          "  core = /cores/mgba.nro  \n"
          "roms = " TEST_DIR "/roms/gba\n"
          "roms = " TEST_DIR "/tico/roms/gba\n"
          "roms = " TEST_DIR "/roms/does-not-exist\n"
          "extensions = gba, AGB\n"
          "\n"
          "[Nintendo 64]\n"
          "core=/cores/mupen.nro\n"
          "roms=" TEST_DIR "/roms/n64\n"
          "extensions=n64,z64,v64\n"
          "args = -L \"{core}\" --rom \"{rom}\"\n"
          "unknown_key = ignored\n", f);
    fclose(f);

    SystemList systems;
    if (!systems_init(&systems)) return 1;

    check("load succeeds", R_SUCCEEDED(systems_load(&systems, TEST_DIR "/systems.ini")));
    check("two sections parsed", systems.count == 2);
    if (systems.count != 2) return 1;

    const System *gba = &systems.items[0];
    const System *n64 = &systems.items[1];

    check("section name parsed", strcmp(gba->name, "Game Boy Advance") == 0);
    check("value whitespace trimmed", strcmp(gba->core, "/cores/mgba.nro") == 0);
    check("repeated roms keys accumulate", gba->roms_count == 3);
    check("single roms key", n64->roms_count == 1);
    check("default args when omitted", strcmp(gba->args, "\"{core}\" \"{rom}\"") == 0);
    check("explicit args override default",
          strcmp(n64->args, "-L \"{core}\" --rom \"{rom}\"") == 0);

    check("extension matches", system_matches(gba, "Zelda.gba"));
    check("extension match is case-insensitive", system_matches(gba, "Metroid.AGB"));
    check("second extension in list matches", system_matches(gba, "x.agb"));
    check("near-miss extension rejected (gbc vs gba)", !system_matches(gba, "cover.gbc"));
    check("unrelated extension rejected", !system_matches(gba, "notes.txt"));
    check("no extension rejected", !system_matches(gba, "README"));
    check("trailing-dot rejected", !system_matches(gba, "weird."));
    check("middle extension in list matches", system_matches(n64, "Mario.z64"));

    char out[512];
    check("args expand both tokens",
          system_expand_args(n64, "/roms/n64/Mario.z64", out, sizeof(out)) &&
          strcmp(out, "-L \"/cores/mupen.nro\" --rom \"/roms/n64/Mario.z64\"") == 0);

    char tiny[8];
    check("args expansion refuses to overflow",
          !system_expand_args(n64, "/roms/n64/Mario.z64", tiny, sizeof(tiny)));

    EntryList list;
    if (!entry_list_init(&list)) return 1;
    check("rom scan succeeds", R_SUCCEEDED(roms_scan(&list, &systems)));

    size_t gba_count = 0, n64_count = 0;
    bool found_nested = false, found_txt = false, found_tico = false, all_games = true;
    for (size_t i = 0; i < list.count; i++) {
        const Entry *e = &list.items[i];
        if (e->kind != EntryKind_Game) all_games = false;
        if (e->system_index == 0) gba_count++;
        if (e->system_index == 1) n64_count++;
        if (strcmp(e->name, "Deep") == 0) found_nested = true;
        if (strstr(e->name, "notes")) found_txt = true;
        if (strcmp(e->name, "FromTico") == 0) found_tico = true;
    }

    check("every entry is a game", all_games);
    check("gba roms found across both libraries", gba_count == 4);
    check("second roms dir (tico layout) scanned", found_tico);
    check("n64 rom found", n64_count == 1);
    check("recursed into subdirectory", found_nested);
    check("non-matching file excluded", !found_txt);

    for (size_t i = 0; i < list.count; i++)
        if (list.items[i].system_index == 0) {
            check("author column carries system name",
                  strcmp(list.items[i].author, "Game Boy Advance") == 0);
            break;
        }

    entry_list_free(&list);
    systems_free(&systems);
    if (system("rm -rf " TEST_DIR) != 0) { /* best effort */ }

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
