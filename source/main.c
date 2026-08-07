#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "icons.h"
#include "launch.h"
#include "render.h"
#include "roms.h"
#include "ui.h"

#define HOMEBREW_ROOT "sdmc:/switch"
#define CONFIG_DIR    "sdmc:/switch/ludi-nx"
#define CONFIG_PATH   CONFIG_DIR "/systems.ini"

static size_t build_view(const EntryList *list, Filter filter, size_t *view)
{
    size_t count = 0;

    for (size_t i = 0; i < list->count; i++) {
        EntryKind kind = list->items[i].kind;

        if (filter == Filter_Homebrew && kind != EntryKind_Homebrew)
            continue;
        if (filter == Filter_Games && kind != EntryKind_Game)
            continue;
        if (filter == Filter_Titles && kind != EntryKind_Title)
            continue;

        view[count++] = i;
    }

    return count;
}

/* Keeps the selected tile's row on screen. */
static void clamp_scroll(size_t selected, size_t *scroll_row)
{
    size_t row = selected / UI_COLS;

    if (row < *scroll_row)
        *scroll_row = row;
    else if (row >= *scroll_row + UI_ROWS)
        *scroll_row = row - UI_ROWS + 1;
}

static void rescan(EntryList *list, SystemList *systems, char *status, size_t status_size)
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

    if (R_FAILED(hb) && R_FAILED(ns))
        snprintf(status, status_size, "could not read %s or the title list", HOMEBREW_ROOT);
    else if (R_FAILED(hb))
        snprintf(status, status_size, "could not read %s", HOMEBREW_ROOT);
    else if (R_FAILED(ns))
        snprintf(status, status_size, "could not list installed games (0x%x)", ns);
    else
        status[0] = '\0';
}

static void move(size_t *selected, size_t view_count, int delta)
{
    if (view_count == 0)
        return;

    long next = (long)*selected + delta;
    if (next < 0)
        next = 0;
    if (next >= (long)view_count)
        next = (long)view_count - 1;

    *selected = (size_t)next;
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
    if (!entry_list_init(&list) || !systems_init(&systems)) {
        icons_destroy(icons);
        render_exit(&render);
        return 1;
    }

    char status[256] = { 0 };
    rescan(&list, &systems, status, sizeof(status));

    if (!launch_can_launch_title() && status[0] == '\0')
        snprintf(status, sizeof(status),
                 "installed games cannot be launched from this applet type");

    size_t *view = malloc((list.count ? list.count : 1) * sizeof(*view));
    size_t view_capacity = list.count;
    size_t view_count = view ? build_view(&list, Filter_All, view) : 0;

    Filter filter = Filter_All;
    size_t selected = 0;
    size_t scroll_row = 0;

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                goto done;

        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus)
            break;

        if (down & HidNpadButton_AnyLeft)  move(&selected, view_count, -1);
        if (down & HidNpadButton_AnyRight) move(&selected, view_count, +1);
        if (down & HidNpadButton_AnyUp)    move(&selected, view_count, -UI_COLS);
        if (down & HidNpadButton_AnyDown)  move(&selected, view_count, +UI_COLS);
        if (down & HidNpadButton_L)        move(&selected, view_count, -UI_PAGE);
        if (down & HidNpadButton_R)        move(&selected, view_count, +UI_PAGE);

        if (down & HidNpadButton_X) {
            filter = (filter + 1) % Filter_Count;
            view_count = view ? build_view(&list, filter, view) : 0;
            selected = 0;
            scroll_row = 0;
        }

        if (down & HidNpadButton_Y) {
            rescan(&list, &systems, status, sizeof(status));

            if (list.count > view_capacity) {
                size_t *grown = realloc(view, list.count * sizeof(*view));
                if (grown) {
                    view = grown;
                    view_capacity = list.count;
                }
            }

            view_count = view ? build_view(&list, filter, view) : 0;
            if (selected >= view_count)
                selected = view_count ? view_count - 1 : 0;
            scroll_row = 0;
        }

        if ((down & HidNpadButton_A) && view_count > 0) {
            const Entry *entry = &list.items[view[selected]];
            Result rc = launch_entry(entry, &systems);

            if (R_SUCCEEDED(rc))
                break;   /* hbloader chainloads, or the system takes over. */

            if (entry->kind == EntryKind_Title && !launch_can_launch_title())
                snprintf(status, sizeof(status),
                         "installed games cannot be launched from this applet type");
            else
                snprintf(status, sizeof(status), "launch failed (0x%x)", rc);
        }

        clamp_scroll(selected, &scroll_row);
        ui_draw(&render, icons, &list, view, view_count, selected, scroll_row,
                filter, status);
    }

done:
    free(view);
    systems_free(&systems);
    entry_list_free(&list);
    icons_destroy(icons);
    render_exit(&render);
    return 0;
}
