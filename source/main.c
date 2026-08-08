#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "icons.h"
#include "launch.h"
#include "overrides.h"
#include "render.h"
#include "roms.h"
#include "shelves.h"
#include "ui.h"

#define HOMEBREW_ROOT "sdmc:/switch"
#define CONFIG_DIR    "sdmc:/switch/vitrine"
#define SYSTEMS_PATH  CONFIG_DIR "/systems.ini"
#define CONFIG_PATH   CONFIG_DIR "/config.json"
#define PAGE_JUMP      6

typedef struct {
    EntryList    list;
    SystemList   systems;
    ShelfList    shelves;
    OverrideList overrides;
    bool         show_hidden;
} Library;

static void library_regroup(Library *lib)
{
    shelves_build(&lib->shelves, &lib->list, &lib->systems, &lib->overrides,
                  lib->show_hidden);
}

static void rescan(Library *lib, char *status, size_t status_size)
{
    lib->list.count = 0;
    lib->systems.count = 0;

    if (R_FAILED(systems_load(&lib->systems, SYSTEMS_PATH))) {
        mkdir(CONFIG_DIR, 0777);
        systems_write_example(SYSTEMS_PATH);
        systems_load(&lib->systems, SYSTEMS_PATH);
    }

    Result hb = homebrew_scan(&lib->list, HOMEBREW_ROOT);
    Result ns = titles_scan(&lib->list);
    roms_scan(&lib->list, &lib->systems);
    entry_list_sort(&lib->list);
    library_regroup(lib);

    if (R_FAILED(hb) && R_FAILED(ns))
        snprintf(status, status_size, "could not read %s or the title list", HOMEBREW_ROOT);
    else if (R_FAILED(hb))
        snprintf(status, status_size, "could not read %s", HOMEBREW_ROOT);
    else if (R_FAILED(ns))
        snprintf(status, status_size, "could not list installed games (0x%x)", ns);
    else
        status[0] = '\0';
}

static void move_cursor(Shelf *shelf, long delta)
{
    long next = (long)shelf->cursor + delta;

    if (next < 0)
        next = 0;
    if (next >= (long)shelf->count)
        next = (long)shelf->count - 1;

    shelf->cursor = (size_t)next;
}

/* Applet mode cannot fit a core, so refuse up front instead of crashing later. */
static void run_mode_gate(Render *render, PadState *pad)
{
    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                return;

        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus)
            return;

        ui_draw_mode_gate(render);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    Render render;
    if (!render_init(&render)) {
        render_exit(&render);
        return 1;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!launch_is_application_mode()) {
        run_mode_gate(&render, &pad);
        render_exit(&render);
        return 0;
    }

    IconCache *icons = icons_create(render.renderer);

    Library lib;
    memset(&lib, 0, sizeof(lib));
    if (!entry_list_init(&lib.list) || !systems_init(&lib.systems) ||
        !shelves_init(&lib.shelves) || !overrides_init(&lib.overrides)) {
        icons_destroy(icons);
        render_exit(&render);
        return 1;
    }

    /* Absent config just means nothing has been customised yet. */
    overrides_load(&lib.overrides, CONFIG_PATH);

    char status[256] = { 0 };
    rescan(&lib, status, sizeof(status));

    size_t shelf_index = 0;
    UiState ui;
    ui_state_init(&ui);

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                goto done;

        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus)
            break;

        if (lib.shelves.count > 0) {
            Shelf *shelf = &lib.shelves.items[shelf_index];
            size_t was_shelf = shelf_index;
            size_t was_cursor = shelf->cursor;

            if (down & HidNpadButton_AnyLeft)  move_cursor(shelf, -1);
            if (down & HidNpadButton_AnyRight) move_cursor(shelf, +1);
            if (down & HidNpadButton_L)        move_cursor(shelf, -PAGE_JUMP);
            if (down & HidNpadButton_R)        move_cursor(shelf, +PAGE_JUMP);

            if ((down & HidNpadButton_AnyUp) && shelf_index > 0)
                shelf_index--;
            if ((down & HidNpadButton_AnyDown) && shelf_index + 1 < lib.shelves.count)
                shelf_index++;

            if (shelf_index != was_shelf || shelf->cursor != was_cursor)
                ui_state_bump(&ui);
        }

        /* Hiding and re-tagging both mutate overrides, so both regroup. */
        if ((down & (HidNpadButton_X | HidNpadButton_ZL)) && lib.shelves.count > 0) {
            const Shelf *shelf = &lib.shelves.items[shelf_index];
            const Entry *entry = &lib.list.items[shelf->items[shelf->cursor]];

            if (down & HidNpadButton_X) {
                overrides_toggle_hidden(&lib.overrides, entry);
            } else if (entry->kind == EntryKind_Homebrew) {
                overrides_toggle_promote(&lib.overrides, entry);
            } else {
                snprintf(status, sizeof(status),
                         "only homebrew can be moved into Installed Games");
            }

            mkdir(CONFIG_DIR, 0777);
            if (R_FAILED(overrides_save(&lib.overrides, CONFIG_PATH)))
                snprintf(status, sizeof(status), "could not write config.json");

            library_regroup(&lib);
            if (shelf_index >= lib.shelves.count)
                shelf_index = lib.shelves.count ? lib.shelves.count - 1 : 0;
            ui_state_bump(&ui);
        }

        if (down & HidNpadButton_ZR) {
            lib.show_hidden = !lib.show_hidden;
            library_regroup(&lib);
            if (shelf_index >= lib.shelves.count)
                shelf_index = lib.shelves.count ? lib.shelves.count - 1 : 0;
            ui_state_bump(&ui);
        }

        if (down & HidNpadButton_Y) {
            rescan(&lib, status, sizeof(status));
            if (shelf_index >= lib.shelves.count)
                shelf_index = lib.shelves.count ? lib.shelves.count - 1 : 0;
            ui_state_bump(&ui);
        }

        if ((down & HidNpadButton_A) && lib.shelves.count > 0) {
            const Shelf *shelf = &lib.shelves.items[shelf_index];
            const Entry *entry = &lib.list.items[shelf->items[shelf->cursor]];
            Result rc = launch_entry(entry, &lib.systems);

            if (R_SUCCEEDED(rc))
                break;   /* hbloader chainloads, or the system takes over. */

            snprintf(status, sizeof(status), "launch failed (0x%x)", rc);
        }

        ui_draw(&render, icons, &lib.list, &lib.shelves, shelf_index, &ui,
                &lib.overrides, lib.show_hidden, status);
    }

done:
    if (lib.overrides.dirty)
        overrides_save(&lib.overrides, CONFIG_PATH);

    overrides_free(&lib.overrides);
    shelves_free(&lib.shelves);
    systems_free(&lib.systems);
    entry_list_free(&lib.list);
    icons_destroy(icons);
    render_exit(&render);
    return 0;
}
