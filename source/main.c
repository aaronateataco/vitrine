#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "launch.h"
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

static void clamp_scroll(size_t selected, size_t *scroll)
{
    if (selected < *scroll)
        *scroll = selected;
    else if (selected >= *scroll + UI_LIST_ROWS)
        *scroll = selected - UI_LIST_ROWS + 1;
}

/*
 * Reloads the system table too, so editing systems.ini and pressing rescan is
 * enough to pick up a newly built core.
 */
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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    EntryList list;
    if (!entry_list_init(&list)) {
        printf("out of memory\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                break;
        }
        consoleExit(NULL);
        return 1;
    }

    SystemList systems;
    if (!systems_init(&systems)) {
        entry_list_free(&list);
        consoleExit(NULL);
        return 1;
    }

    char status[256] = {0};
    rescan(&list, &systems, status, sizeof(status));

    if (!launch_can_launch_title() && status[0] == '\0')
        snprintf(status, sizeof(status),
                 "games cannot be launched from this applet type - homebrew only");

    size_t *view = malloc((list.count ? list.count : 1) * sizeof(*view));
    size_t view_capacity = list.count;
    size_t view_count = view ? build_view(&list, Filter_All, view) : 0;

    Filter filter = Filter_All;
    size_t selected = 0;
    size_t scroll = 0;
    bool redraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus)
            break;

        if (view_count > 0) {
            if (down & HidNpadButton_AnyDown) {
                selected = (selected + 1) % view_count;
                redraw = true;
            }
            if (down & HidNpadButton_AnyUp) {
                selected = (selected + view_count - 1) % view_count;
                redraw = true;
            }
            if (down & HidNpadButton_AnyRight) {
                selected = (selected + UI_LIST_ROWS < view_count)
                               ? selected + UI_LIST_ROWS
                               : view_count - 1;
                redraw = true;
            }
            if (down & HidNpadButton_AnyLeft) {
                selected = (selected > UI_LIST_ROWS) ? selected - UI_LIST_ROWS : 0;
                redraw = true;
            }
        }

        if (down & HidNpadButton_X) {
            filter = (filter + 1) % Filter_Count;
            view_count = view ? build_view(&list, filter, view) : 0;
            selected = 0;
            scroll = 0;
            redraw = true;
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
            scroll = 0;
            redraw = true;
        }

        if ((down & HidNpadButton_A) && view_count > 0) {
            const Entry *entry = &list.items[view[selected]];
            Result rc = launch_entry(entry, &systems);

            if (R_SUCCEEDED(rc)) {
                /*
                 * Both paths hand control away: hbloader chainloads the next NRO
                 * once we return, and the system takes over for a title launch.
                 */
                break;
            }

            if (entry->kind == EntryKind_Title && !launch_can_launch_title())
                snprintf(status, sizeof(status),
                         "games cannot be launched from this applet type");
            else
                snprintf(status, sizeof(status), "launch failed (0x%x)", rc);

            redraw = true;
        }

        if (redraw) {
            clamp_scroll(selected, &scroll);
            ui_draw(&list, view, view_count, selected, scroll, filter, status);
            redraw = false;
        }

        consoleUpdate(NULL);
    }

    free(view);
    systems_free(&systems);
    entry_list_free(&list);
    consoleExit(NULL);
    return 0;
}
