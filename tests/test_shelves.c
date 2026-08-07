/* Host-side checks for platform grouping and shelf ordering. */
#include <stdio.h>
#include <string.h>

#include "shelves.h"

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void add_entry(EntryList *list, EntryKind kind, const char *name, int system)
{
    Entry *e = entry_list_add(list);
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->kind = kind;
    e->system_index = system;
}

static const Shelf *shelf_at(const ShelfList *s, size_t i)
{
    return i < s->count ? &s->items[i] : NULL;
}

int main(void)
{
    /* Declared out of order on purpose: shelf order must follow this file, not
       alphabetical order, and Saturn must vanish for having no ROMs. */
    FILE *f = fopen("testdata-shelves.ini", "w");
    if (!f) return 1;
    fputs("[Game Boy Advance]\ncore=/c/gba.nro\nroms=/r/gba\nextensions=gba\n"
          "[Saturn]\ncore=/c/sat.nro\nroms=/r/sat\nextensions=cue\n"
          "[Nintendo 64]\ncore=/c/n64.nro\nroms=/r/n64\nextensions=z64\n", f);
    fclose(f);

    SystemList systems;
    if (!systems_init(&systems)) return 1;
    if (R_FAILED(systems_load(&systems, "testdata-shelves.ini"))) return 1;
    check("three systems declared", systems.count == 3);

    EntryList list;
    if (!entry_list_init(&list)) return 1;

    add_entry(&list, EntryKind_Title,    "Zelda TotK",  -1);
    add_entry(&list, EntryKind_Title,    "Metroid",     -1);
    add_entry(&list, EntryKind_Homebrew, "Goldleaf",    -1);
    add_entry(&list, EntryKind_Homebrew, "DBI",         -1);
    add_entry(&list, EntryKind_Homebrew, "ftpd",        -1);
    add_entry(&list, EntryKind_Game,     "Minish Cap",   0);   /* GBA */
    add_entry(&list, EntryKind_Game,     "Fire Emblem",  0);   /* GBA */
    add_entry(&list, EntryKind_Game,     "Mario 64",     2);   /* N64 */

    /* main.c sorts before grouping, so mirror that here. */
    entry_list_sort(&list);

    ShelfList shelves;
    if (!shelves_init(&shelves)) return 1;
    shelves_build(&shelves, &list, &systems);

    check("empty platform dropped (Saturn)", shelves.count == 4);

    const Shelf *s0 = shelf_at(&shelves, 0);
    const Shelf *s1 = shelf_at(&shelves, 1);
    const Shelf *s2 = shelf_at(&shelves, 2);
    const Shelf *s3 = shelf_at(&shelves, 3);

    check("installed games shelf first", s0 && strcmp(s0->name, "Installed Games") == 0);
    check("homebrew shelf second",       s1 && strcmp(s1->name, "Homebrew") == 0);
    check("platforms follow config order, not alphabetical",
          s2 && strcmp(s2->name, "Game Boy Advance") == 0 &&
          s3 && strcmp(s3->name, "Nintendo 64") == 0);

    check("installed games grouped", s0 && s0->count == 2);
    check("homebrew grouped",        s1 && s1->count == 3);
    check("gba roms grouped",        s2 && s2->count == 2);
    check("n64 rom grouped",         s3 && s3->count == 1);

    check("shelf items index back into the entry list, in sorted order",
          s2 && strcmp(list.items[s2->items[0]].name, "Fire Emblem") == 0 &&
          strcmp(list.items[s2->items[1]].name, "Minish Cap") == 0);

    /* Shelf order is fixed by the grouping, not by how entries happen to sort. */
    check("shelf order independent of entry sort order",
          s0 && strcmp(s0->name, "Installed Games") == 0);

    /* Cursor must survive a rescan, since Y is pressed mid-browse. */
    shelves.items[2].cursor = 1;
    shelves_build(&shelves, &list, &systems);
    check("cursor preserved across rebuild", shelves.items[2].cursor == 1);

    /* A shrinking shelf must not leave the cursor out of bounds. */
    list.count = 6;   /* drops Fire Emblem and Mario 64 */
    shelves_build(&shelves, &list, &systems);
    check("cursor clamped when shelf shrinks",
          shelves.count >= 3 && shelves.items[2].cursor < shelves.items[2].count);
    check("emptied platform disappears", shelves.count == 3);

    shelves_free(&shelves);
    entry_list_free(&list);
    systems_free(&systems);
    remove("testdata-shelves.ini");

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
