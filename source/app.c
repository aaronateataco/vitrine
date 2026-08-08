#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "diag.h"
#include "roms.h"

void app_regroup(App *app)
{
    shelves_build(&app->lib.shelves, &app->lib.list, &app->lib.systems,
                  &app->lib.overrides, app->lib.overrides.prefs.show_hidden);
}

void app_status(App *app, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, args);
    va_end(args);
}

void app_clamp_shelf(App *app)
{
    if (app->shelf_index >= app->lib.shelves.count)
        app->shelf_index = app->lib.shelves.count ? app->lib.shelves.count - 1 : 0;
}

const Shelf *app_shelf(const App *app)
{
    if (app->shelf_index >= app->lib.shelves.count)
        return NULL;
    return &app->lib.shelves.items[app->shelf_index];
}

const Entry *app_entry(const App *app)
{
    const Shelf *shelf = app_shelf(app);
    if (!shelf || shelf->cursor >= shelf->count)
        return NULL;
    return &app->lib.list.items[shelf->items[shelf->cursor]];
}

void app_save_config(App *app)
{
    mkdir(CONFIG_DIR, 0777);
    if (R_FAILED(overrides_save(&app->lib.overrides, CONFIG_PATH)))
        app_status(app, "could not write config.json");
}

void app_rescan(App *app)
{
    Library *lib = &app->lib;

    lib->list.count = 0;
    lib->systems.count = 0;

    if (R_FAILED(systems_load(&lib->systems, SYSTEMS_PATH))) {
        mkdir(CONFIG_DIR, 0777);
        systems_write_example(SYSTEMS_PATH);
        systems_load(&lib->systems, SYSTEMS_PATH);
    }

    /* Before the generic sweep, so claimed directories are attributed and the
       dedupe in homebrew_scan_dir keeps them out of Homebrew. */
    for (size_t i = 0; i < lib->systems.count; i++)
        for (size_t d = 0; d < lib->systems.items[i].nro_count; d++)
            homebrew_scan_dir(&lib->list, lib->systems.items[i].nro[d], (int)i);

    Result hb = homebrew_scan(&lib->list, HOMEBREW_ROOT);
    Result ns = titles_scan(&lib->list);
    roms_scan(&lib->list, &lib->systems);
    entry_list_sort(&lib->list);
    app_regroup(app);

    if (R_FAILED(hb) && R_FAILED(ns))
        app_status(app, "could not read %s or the title list", HOMEBREW_ROOT);
    else if (R_FAILED(hb))
        app_status(app, "could not read %s", HOMEBREW_ROOT);
    else if (R_FAILED(ns))
        app_status(app, "could not list installed games (0x%x)", ns);
    else
        app->status[0] = '\0';
}

/*
 * Surfaced in Settings: a missing core is the usual reason a ROM will not
 * start, and it is otherwise invisible until you press A.
 */
void app_note_missing_cores(App *app)
{
    app->core_note[0] = '\0';

    for (size_t i = 0; i < app->lib.systems.count; i++) {
        const System *system = &app->lib.systems.items[i];

        if (system->core_count && !system_pick_core(system)) {
            snprintf(app->core_note, sizeof(app->core_note),
                     "No core found for %s - check systems.ini", system->name);
            return;
        }
    }
}
