#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "diag.h"
#include "launch.h"
#include "net.h"

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

static bool library_init(App *app)
{
    return entry_list_init(&app->lib.list) && systems_init(&app->lib.systems) &&
           shelves_init(&app->lib.shelves) && overrides_init(&app->lib.overrides);
}

static void library_free(App *app)
{
    overrides_free(&app->lib.overrides);
    shelves_free(&app->lib.shelves);
    systems_free(&app->lib.systems);
    entry_list_free(&app->lib.list);
}

static void load_rooms(App *app)
{
    if (!rooms_init(&app->rooms))
        return;

    if (R_FAILED(rooms_load(&app->rooms, ROOMS_PATH))) {
        mkdir(CONFIG_DIR, 0777);
        rooms_write_example(ROOMS_PATH);
        rooms_load(&app->rooms, ROOMS_PATH);
    }

    diag_logf("rooms: %zu defined", app->rooms.count);
}

/* Hiding and re-tagging both mutate overrides, so both regroup and save. */
static void toggle_entry_flags(App *app, u64 down)
{
    const Entry *entry = app_entry(app);
    if (!entry)
        return;

    if (down & HidNpadButton_X) {
        overrides_toggle_hidden(&app->lib.overrides, entry);
    } else if (entry->kind == EntryKind_Homebrew) {
        overrides_toggle_promote(&app->lib.overrides, entry);
    } else {
        app_status(app, "only homebrew can be moved into Installed Games");
        return;
    }

    app_save_config(app);
    app_regroup(app);
    app_clamp_shelf(app);
    ui_state_bump(&app->ui);
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

static void navigate(App *app, u64 down)
{
    if (app->lib.shelves.count == 0)
        return;

    Shelf *shelf = &app->lib.shelves.items[app->shelf_index];
    size_t was_shelf = app->shelf_index;
    size_t was_cursor = shelf->cursor;

    if (down & HidNpadButton_AnyLeft)  move_cursor(shelf, -1);
    if (down & HidNpadButton_AnyRight) move_cursor(shelf, +1);
    if (down & HidNpadButton_L)        move_cursor(shelf, -PAGE_JUMP);
    if (down & HidNpadButton_R)        move_cursor(shelf, +PAGE_JUMP);

    if ((down & HidNpadButton_AnyUp) && app->shelf_index > 0)
        app->shelf_index--;
    if ((down & HidNpadButton_AnyDown) && app->shelf_index + 1 < app->lib.shelves.count)
        app->shelf_index++;

    if (app->shelf_index != was_shelf || shelf->cursor != was_cursor)
        ui_state_bump(&app->ui);
}

/* Returns true when a launch succeeded and the app should hand off. */
static bool try_launch(App *app)
{
    const Entry *entry = app_entry(app);
    if (!entry)
        return false;

    diag_logf("launch kind=%d \"%s\" path=%s", entry->kind, entry->name,
              entry->path[0] ? entry->path : "(title)");

    Result rc = launch_entry(entry, &app->lib.systems);
    diag_logf("launch result 0x%x", rc);

    if (R_SUCCEEDED(rc))
        return true;   /* hbloader chainloads, or the system takes over. */

    app_status(app, "launch failed (0x%x)", rc);
    return false;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    mkdir(CONFIG_DIR, 0777);
    diag_open(LOG_PATH);
    diag_logf("applet mode: %s", launch_is_application_mode() ? "Application" : "other");

    Render render;
    if (!render_init(&render)) {
        diag_logf("render_init FAILED: %s", SDL_GetError());
        diag_close();
        render_exit(&render);
        return 1;
    }
    diag_logf("render ready");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!launch_is_application_mode()) {
        diag_logf("refusing to run: not Application Mode");
        run_mode_gate(&render, &pad);
        diag_close();
        render_exit(&render);
        return 0;
    }

    App app;
    memset(&app, 0, sizeof(app));
    app.render = &render;
    app.pad = &pad;
    app.icons = icons_create(render.renderer, COVERS_DIR);

    if (!library_init(&app)) {
        icons_destroy(app.icons);
        render_exit(&render);
        diag_close();
        return 1;
    }

    /* 3D is optional: if the context or shaders fail, the rooms report
       themselves unavailable and the rest of the app carries on. */
    app.scene = scene_create();
    scene_camera_reset(&app.camera);
    load_rooms(&app);
    ui_state_init(&app.ui);

    /* Absent config just means nothing has been customised yet. */
    overrides_load(&app.lib.overrides, CONFIG_PATH);

    app_rescan(&app);
    app_note_missing_cores(&app);

    diag_logf("scan: %zu entries across %zu shelves", app.lib.list.count,
              app.lib.shelves.count);
    for (size_t i = 0; i < app.lib.systems.count; i++) {
        const char *core = system_pick_core(&app.lib.systems.items[i]);
        diag_logf("system %-28s core=%s", app.lib.systems.items[i].name,
                  core ? core : "NONE FOUND");
    }

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                goto done;

        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus)
            break;

        /* Before the modal dispatch, so a screenshot can be taken of the 3D
           rooms and the settings overlay, not just the library. */
        if (down & HidNpadButton_StickL) {
            char shot[512];
            if (diag_screenshot(render.renderer, SHOTS_DIR, shot, sizeof(shot)))
                app_status(&app, "saved %s", shot);
            else
                app_status(&app, "screenshot failed");
        }

        /* Modal views, most-nested first. Each returns true when it owns the
           frame; falling off the end means the library is in control. */
        if (view_trophy_update(&app, down))   continue;
        if (view_room_update(&app, down))     continue;
        if (view_picker_update(&app, down))   continue;
        if (view_settings_update(&app, down)) continue;

        if (down & HidNpadButton_Minus) {
            app.settings.open = true;
            app.settings.row = 0;
            continue;
        }

        if ((down & HidNpadButton_B) && app.lib.shelves.count > 0) {
            view_room_open(&app);
            continue;
        }

        if ((down & HidNpadButton_StickR) && app.lib.shelves.count > 0) {
            view_picker_open(&app);
            continue;
        }

        navigate(&app, down);

        if (down & (HidNpadButton_X | HidNpadButton_ZL))
            toggle_entry_flags(&app, down);

        if (down & HidNpadButton_ZR) {
            app.lib.overrides.prefs.show_hidden = !app.lib.overrides.prefs.show_hidden;
            app_regroup(&app);
            app_clamp_shelf(&app);
            ui_state_bump(&app.ui);
        }

        if (down & HidNpadButton_Y) {
            app_rescan(&app);
            app_note_missing_cores(&app);
            app_clamp_shelf(&app);
            ui_state_bump(&app.ui);
        }

        if ((down & HidNpadButton_A) && try_launch(&app))
            break;

        ui_draw(&render, app.icons, &app.lib.list, &app.lib.shelves,
                app.shelf_index, &app.ui, &app.lib.overrides, app.status);
    }

done:
    view_picker_close(&app);
    view_trophy_close(&app);
    rooms_free(&app.rooms);
    scene_destroy(app.scene);

    if (app.lib.overrides.dirty)
        overrides_save(&app.lib.overrides, CONFIG_PATH);

    library_free(&app);
    icons_destroy(app.icons);
    render_exit(&render);

    net_exit();
    diag_logf("clean exit");
    diag_close();
    return 0;
}
