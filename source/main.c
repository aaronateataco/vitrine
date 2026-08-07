#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "icons.h"
#include "launch.h"
#include "render.h"
#include "roms.h"
#include "shelves.h"
#include "ui.h"

#define HOMEBREW_ROOT "sdmc:/switch"
#define CONFIG_DIR    "sdmc:/switch/ludi-nx"
#define CONFIG_PATH   CONFIG_DIR "/systems.ini"
#define PAGE_JUMP      6

static void rescan(EntryList *list, SystemList *systems, ShelfList *shelves,
                   char *status, size_t status_size)
{
    list->count = 0;
    systems->count = 0;

    if (R_FAILED(systems_load(systems, CONFIG_PATH))) {
        mkdir(CONFIG_DIR, 0777);
        systems_write_example(CONFIG_PATH);
        systems_load(systems, CONFIG_PATH);
    }

    Result hb = homebrew_scan(list, HOMEBREW_ROOT);
    Result ns = titles_scan(list);
    roms_scan(list, systems);
    entry_list_sort(list);
    shelves_build(shelves, list, systems);

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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    Render render;
    if (!render_init(&render)) {
        render_exit(&render);
        return 1;
    }

    IconCache *icons = icons_create(render.renderer);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    EntryList list;
    SystemList systems;
    ShelfList shelves;
    if (!entry_list_init(&list) || !systems_init(&systems) || !shelves_init(&shelves)) {
        icons_destroy(icons);
        render_exit(&render);
        return 1;
    }

    char status[256] = { 0 };
    rescan(&list, &systems, &shelves, status, sizeof(status));

    if (!launch_can_launch_title() && status[0] == '\0')
        snprintf(status, sizeof(status),
                 "installed games cannot be launched from this applet type");

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

        if (shelves.count > 0) {
            Shelf *shelf = &shelves.items[shelf_index];
            size_t before_shelf = shelf_index;
            size_t before_cursor = shelf->cursor;

            if (down & HidNpadButton_AnyLeft)  move_cursor(shelf, -1);
            if (down & HidNpadButton_AnyRight) move_cursor(shelf, +1);
            if (down & HidNpadButton_L)        move_cursor(shelf, -PAGE_JUMP);
            if (down & HidNpadButton_R)        move_cursor(shelf, +PAGE_JUMP);

            if ((down & HidNpadButton_AnyUp) && shelf_index > 0)
                shelf_index--;
            if ((down & HidNpadButton_AnyDown) && shelf_index + 1 < shelves.count)
                shelf_index++;

            if (shelf_index != before_shelf || shelf->cursor != before_cursor)
                ui_state_bump(&ui);
        }

        if (down & HidNpadButton_Y) {
            rescan(&list, &systems, &shelves, status, sizeof(status));
            if (shelf_index >= shelves.count)
                shelf_index = shelves.count ? shelves.count - 1 : 0;
            ui_state_bump(&ui);
        }

        if ((down & HidNpadButton_A) && shelves.count > 0) {
            const Shelf *shelf = &shelves.items[shelf_index];
            const Entry *entry = &list.items[shelf->items[shelf->cursor]];
            Result rc = launch_entry(entry, &systems);

            if (R_SUCCEEDED(rc))
                break;   /* hbloader chainloads, or the system takes over. */

            if (entry->kind == EntryKind_Title && !launch_can_launch_title())
                snprintf(status, sizeof(status),
                         "installed games cannot be launched from this applet type");
            else
                snprintf(status, sizeof(status), "launch failed (0x%x)", rc);
        }

        ui_draw(&render, icons, &list, &shelves, shelf_index, &ui, status);
    }

done:
    shelves_free(&shelves);
    systems_free(&systems);
    entry_list_free(&list);
    icons_destroy(icons);
    render_exit(&render);
    return 0;
}
