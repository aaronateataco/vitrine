/* Host-side checks for config.json persistence, hiding and re-tagging. */
#include <stdio.h>
#include <string.h>

#include "overrides.h"

#define CONFIG "testdata-config.json"

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static Entry make_homebrew(const char *path)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    e.kind = EntryKind_Homebrew;
    e.system_index = -1;
    snprintf(e.path, sizeof(e.path), "%s", path);
    return e;
}

static Entry make_title(u64 id)
{
    Entry e;
    memset(&e, 0, sizeof(e));
    e.kind = EntryKind_Title;
    e.system_index = -1;
    e.application_id = id;
    return e;
}

int main(void)
{
    remove(CONFIG);

    Entry hb = make_homebrew("sdmc:/switch/dbi/DBI.nro");
    Entry rom = make_homebrew("sdmc:/tico/roms/gba/Zelda.gba");
    Entry title = make_title(0x0100000000010000ULL);

    char key[ENTRY_PATH_LEN];
    overrides_key(&hb, key, sizeof(key));
    check("homebrew keyed by path", strcmp(key, "sdmc:/switch/dbi/DBI.nro") == 0);

    overrides_key(&title, key, sizeof(key));
    check("title keyed by application id, not path",
          strcmp(key, "title:0100000000010000") == 0);

    OverrideList o;
    if (!overrides_init(&o)) return 1;

    check("nothing hidden by default", !overrides_hidden(&o, &hb));
    check("load of absent file is not fatal", R_FAILED(overrides_load(&o, CONFIG)));

    overrides_toggle_hidden(&o, &hb);
    check("hide toggles on", overrides_hidden(&o, &hb));
    check("hiding marks the list dirty", o.dirty);
    check("hiding one entry leaves others alone", !overrides_hidden(&o, &title));

    overrides_toggle_hidden(&o, &hb);
    check("hide toggles back off", !overrides_hidden(&o, &hb));

    overrides_toggle_hidden(&o, &hb);
    overrides_toggle_hidden(&o, &title);
    overrides_toggle_promote(&o, &hb);
    check("homebrew can be promoted", overrides_promoted(&o, &hb));

    /* Promotion is meaningless for anything that is not homebrew. */
    overrides_toggle_promote(&o, &title);
    check("titles cannot be promoted", !overrides_promoted(&o, &title));
    rom.kind = EntryKind_Game;
    overrides_toggle_promote(&o, &rom);
    check("roms cannot be promoted", !overrides_promoted(&o, &rom));

    /* Display prefs ride in the same file. */
    o.prefs.poster_tiles = true;
    o.prefs.cover_size = 2;
    o.prefs.layout = 1;
    o.prefs.show_hidden = true;

    /* Pinned covers persist alongside the flags. */
    check("no cover pinned by default", overrides_cover(&o, &title)[0] == '\0');
    overrides_set_cover(&o, &title, "https://cdn.example/grid/42.png");
    check("cover pinned",
          strcmp(overrides_cover(&o, &title), "https://cdn.example/grid/42.png") == 0);

    check("save succeeds", R_SUCCEEDED(overrides_save(&o, CONFIG)));
    check("save clears dirty", !o.dirty);

    /* Round-trip through a completely fresh list. */
    OverrideList loaded;
    if (!overrides_init(&loaded)) return 1;
    check("reload succeeds", R_SUCCEEDED(overrides_load(&loaded, CONFIG)));
    check("hidden homebrew persisted", overrides_hidden(&loaded, &hb));
    check("hidden title persisted", overrides_hidden(&loaded, &title));
    check("promotion persisted", overrides_promoted(&loaded, &hb));
    check("reload starts clean", !loaded.dirty);
    check("poster preference persisted", loaded.prefs.poster_tiles);
    check("cover size persisted", loaded.prefs.cover_size == 2);
    check("layout persisted", loaded.prefs.layout == 1);
    check("show-hidden preference persisted", loaded.prefs.show_hidden);
    check("pinned cover persisted",
          strcmp(overrides_cover(&loaded, &title), "https://cdn.example/grid/42.png") == 0);

    /* Unhide-all clears hidden flags without disturbing promotion. */
    overrides_unhide_all(&loaded);
    check("unhide all clears homebrew", !overrides_hidden(&loaded, &hb));
    check("unhide all clears titles", !overrides_hidden(&loaded, &title));
    check("unhide all leaves promotion alone", overrides_promoted(&loaded, &hb));
    check("unhide all marks dirty", loaded.dirty);

    /* Entries back at their defaults carry no information and are not written. */
    OverrideList sparse;
    if (!overrides_init(&sparse)) return 1;
    overrides_toggle_hidden(&sparse, &rom);
    overrides_toggle_hidden(&sparse, &rom);   /* back to default */
    overrides_save(&sparse, CONFIG);

    FILE *f = fopen(CONFIG, "rb");
    char buf[2048] = { 0 };
    if (f) { fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }
    check("default-valued entries omitted from the file",
          strstr(buf, "Zelda.gba") == NULL);
    check("file is valid json with a version", strstr(buf, "\"version\"") != NULL);

    /* A pinned cover alone must keep a record alive, even with no flags set. */
    OverrideList covered;
    if (!overrides_init(&covered)) return 1;
    overrides_set_cover(&covered, &rom, "https://cdn.example/icon/7.png");
    overrides_save(&covered, CONFIG);

    OverrideList recovered;
    if (!overrides_init(&recovered)) return 1;
    overrides_load(&recovered, CONFIG);
    check("cover-only record survives a round trip",
          strcmp(overrides_cover(&recovered, &rom),
                 "https://cdn.example/icon/7.png") == 0);
    overrides_free(&recovered);
    overrides_free(&covered);

    /* A corrupt file must be rejected, not silently half-applied. */
    f = fopen(CONFIG, "wb");
    if (f) { fputs("{ this is not json", f); fclose(f); }
    OverrideList broken;
    if (!overrides_init(&broken)) return 1;
    check("corrupt config rejected", R_FAILED(overrides_load(&broken, CONFIG)));
    check("corrupt config leaves no entries", broken.count == 0);

    overrides_free(&broken);
    overrides_free(&sparse);
    overrides_free(&loaded);
    overrides_free(&o);
    remove(CONFIG);

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
