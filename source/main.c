#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL_image.h>

#include "config.h"
#include "diag.h"
#include "icons.h"
#include "keyboard.h"
#include "net.h"
#include "sgdb.h"
#include "launch.h"
#include "overrides.h"
#include "render.h"
#include "roms.h"
#include "scene.h"
#include "shelves.h"
#include "ui.h"

#define HOMEBREW_ROOT "sdmc:/switch"
#define CONFIG_DIR    "sdmc:/switch/vitrine"
#define SYSTEMS_PATH  CONFIG_DIR "/systems.ini"
#define CONFIG_PATH   CONFIG_DIR "/config.json"
#define LOG_PATH      CONFIG_DIR "/vitrine.log"
#define REPORT_PATH   CONFIG_DIR "/diagnostics.txt"
#define SHOTS_DIR     CONFIG_DIR "/screenshots"
#define COVERS_DIR    CONFIG_DIR "/covers"
#define PAGE_JUMP      6

typedef struct {
    EntryList    list;
    SystemList   systems;
    ShelfList    shelves;
    OverrideList overrides;   /* also carries the display prefs */
} Library;

static void library_regroup(Library *lib)
{
    shelves_build(&lib->shelves, &lib->list, &lib->systems, &lib->overrides,
                  lib->overrides.prefs.show_hidden);
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

    /* Before the generic sweep, so claimed directories are attributed and the
       dedupe in homebrew_scan_dir keeps them out of Homebrew. */
    for (size_t i = 0; i < lib->systems.count; i++)
        for (size_t d = 0; d < lib->systems.items[i].nro_count; d++)
            homebrew_scan_dir(&lib->list, lib->systems.items[i].nro[d], (int)i);

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

/*
 * Downloads covers for the visible shelf. This blocks on the network, so it
 * repaints after each entry with a running count instead of freezing.
 */
static void fetch_shelf_covers(Library *lib, size_t shelf_index, Render *render,
                               const Settings *settings, IconCache *icons,
                               const char *core_note, char *status, size_t status_size)
{
    const Prefs *prefs = &lib->overrides.prefs;

    if (!prefs->sgdb_key[0]) {
        snprintf(status, status_size, "set a SteamGridDB API key first");
        return;
    }
    if (shelf_index >= lib->shelves.count) {
        snprintf(status, status_size, "no shelf selected");
        return;
    }
    if (!net_init()) {
        snprintf(status, status_size, "network unavailable");
        return;
    }

    const Shelf *shelf = &lib->shelves.items[shelf_index];
    size_t got = 0;
    size_t skipped = 0;

    for (size_t i = 0; i < shelf->count; i++) {
        const Entry *entry = &lib->list.items[shelf->items[i]];

        /* Already cached: leave it alone so repeat runs are cheap. */
        if (sgdb_cached(entry, prefs->poster_tiles, COVERS_DIR)) {
            skipped++;
            continue;
        }

        snprintf(status, status_size, "covers %zu/%zu: %s",
                 i + 1, shelf->count, entry->name);
        ui_draw_settings(render, settings, prefs, &lib->list, &lib->shelves,
                         &lib->overrides, core_note);

        if (sgdb_fetch_for_entry(prefs->sgdb_key, entry, prefs->poster_tiles,
                                 COVERS_DIR,
                                 overrides_cover(&lib->overrides, entry), NULL, 0))
            got++;
    }

    icons_flush(icons);
    snprintf(status, status_size, "covers: %zu downloaded, %zu already cached",
             got, skipped);
    diag_logf("cover fetch for \"%s\": %zu new, %zu cached",
              shelf->name, got, skipped);
}

/*
 * Cover picker. The candidate list is cheap, but each preview is a download, so
 * previews are fetched only for whatever is highlighted.
 */
static void picker_close(CoverPicker *picker)
{
    if (picker->preview) {
        SDL_DestroyTexture(picker->preview);
        picker->preview = NULL;
    }
    picker->open = false;
    picker->covers.count = 0;
    picker->index = 0;
    picker->message[0] = '\0';
}

static void picker_open(CoverPicker *picker, Library *lib, size_t shelf_index,
                        char *status, size_t status_size)
{
    const Prefs *prefs = &lib->overrides.prefs;

    picker_close(picker);

    if (!prefs->sgdb_key[0]) {
        snprintf(status, status_size, "set a SteamGridDB API key first");
        return;
    }
    if (shelf_index >= lib->shelves.count) {
        return;
    }
    if (!net_init()) {
        snprintf(status, status_size, "network unavailable");
        return;
    }

    const Shelf *shelf = &lib->shelves.items[shelf_index];
    const Entry *entry = &lib->list.items[shelf->items[shelf->cursor]];

    picker->open = true;
    picker->preview_index = (size_t)-1;

    int game_id = 0;
    if (!sgdb_find_game(prefs->sgdb_key, entry->name, &game_id)) {
        snprintf(picker->message, sizeof(picker->message),
                 "no SteamGridDB match for this title");
        return;
    }

    if (!sgdb_list_covers(prefs->sgdb_key, game_id, prefs->poster_tiles,
                          &picker->covers))
        snprintf(picker->message, sizeof(picker->message), "no covers returned");
}

static void picker_preview(CoverPicker *picker, Render *render)
{
    if (picker->covers.count == 0 || picker->preview_index == picker->index)
        return;

    if (picker->preview) {
        SDL_DestroyTexture(picker->preview);
        picker->preview = NULL;
    }

    char temp[512];
    snprintf(temp, sizeof(temp), "%s/.preview.png", COVERS_DIR);
    mkdir(COVERS_DIR, 0777);

    if (net_download(picker->covers.items[picker->index].url, temp))
        picker->preview = IMG_LoadTexture(render->renderer, temp);

    picker->preview_index = picker->index;
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

    IconCache *icons = icons_create(render.renderer, COVERS_DIR);

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
    diag_logf("scan: %zu entries across %zu shelves", lib.list.count, lib.shelves.count);
    for (size_t i = 0; i < lib.systems.count; i++) {
        const char *core = system_pick_core(&lib.systems.items[i]);
        diag_logf("system %-28s core=%s", lib.systems.items[i].name,
                  core ? core : "NONE FOUND");
    }

    size_t shelf_index = 0;
    UiState ui;
    ui_state_init(&ui);

    Settings settings = { false, 0 };

    /* 3D is optional: if the context or shaders fail, the room is simply
       unavailable and the rest of the app carries on. */
    Scene *scene = scene_create();
    SceneCamera camera;
    scene_camera_reset(&camera);
    bool room_open = false;
    u64 room_frames = 0;

    CoverPicker picker;
    memset(&picker, 0, sizeof(picker));

    /* Surfaced in Settings: a missing core is the usual reason a ROM will not
       start, and it is otherwise invisible until you press A. */
    char core_note[256] = { 0 };
    for (size_t i = 0; i < lib.systems.count; i++) {
        if (lib.systems.items[i].core_count && !system_pick_core(&lib.systems.items[i])) {
            snprintf(core_note, sizeof(core_note),
                     "No core found for %s - check systems.ini",
                     lib.systems.items[i].name);
            break;
        }
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

        if (room_open) {
            if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
                room_open = false;
                continue;
            }

            /* Held, not pressed: orbiting wants continuous input. */
            u64 held = padGetButtons(&pad);
            float dyaw = 0.0f;
            float dpitch = 0.0f;
            float dzoom = 0.0f;

            if (held & HidNpadButton_AnyLeft)  dyaw   -= 0.03f;
            if (held & HidNpadButton_AnyRight) dyaw   += 0.03f;
            if (held & HidNpadButton_AnyUp)    dpitch += 0.02f;
            if (held & HidNpadButton_AnyDown)  dpitch -= 0.02f;
            if (held & HidNpadButton_L)        dzoom  += 0.12f;
            if (held & HidNpadButton_R)        dzoom  -= 0.12f;

            scene_camera_orbit(&camera, dyaw, dpitch, dzoom);

            const Shelf *shelf = &lib.shelves.items[shelf_index];
            char subtitle[96];
            snprintf(subtitle, sizeof(subtitle), "%zu items", shelf->count);

            SDL_Rect viewport = ui_room_begin(&render, &lib.overrides.prefs);
            scene_draw(scene, &render, &camera, viewport,
                       (float)room_frames / 60.0f);
            ui_room_end(&render, &lib.overrides.prefs, shelf->name, subtitle,
                        "Placeholder model - franchises.json wiring is next");
            room_frames++;
            continue;
        }

        if ((down & HidNpadButton_B) && lib.shelves.count > 0) {
            if (!scene) {
                snprintf(status, sizeof(status), "3D unavailable on this build");
            } else {
                room_open = true;
                room_frames = 0;
                scene_camera_reset(&camera);
            }
            continue;
        }

        if (picker.open) {
            if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
                picker_close(&picker);
            } else if (picker.covers.count > 0) {
                if (down & HidNpadButton_AnyUp)
                    picker.index = picker.index ? picker.index - 1
                                                : picker.covers.count - 1;
                if (down & HidNpadButton_AnyDown)
                    picker.index = (picker.index + 1) % picker.covers.count;

                if (down & HidNpadButton_A) {
                    const Shelf *shelf = &lib.shelves.items[shelf_index];
                    const Entry *entry = &lib.list.items[shelf->items[shelf->cursor]];
                    const char *url = picker.covers.items[picker.index].url;

                    /* Pin it, then fetch through the normal path so the cache
                       filename matches what the library will look for. */
                    overrides_set_cover(&lib.overrides, entry, url);
                    mkdir(CONFIG_DIR, 0777);
                    overrides_save(&lib.overrides, CONFIG_PATH);

                    if (sgdb_fetch_for_entry(lib.overrides.prefs.sgdb_key, entry,
                                             lib.overrides.prefs.poster_tiles,
                                             COVERS_DIR, url, NULL, 0)) {
                        icons_flush(icons);
                        snprintf(status, sizeof(status), "cover locked for %s",
                                 entry->name);
                        picker_close(&picker);
                    } else {
                        snprintf(picker.message, sizeof(picker.message),
                                 "download failed");
                    }
                }
            }

            if (room_open) {
            if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
                room_open = false;
                continue;
            }

            /* Held, not pressed: orbiting wants continuous input. */
            u64 held = padGetButtons(&pad);
            float dyaw = 0.0f;
            float dpitch = 0.0f;
            float dzoom = 0.0f;

            if (held & HidNpadButton_AnyLeft)  dyaw   -= 0.03f;
            if (held & HidNpadButton_AnyRight) dyaw   += 0.03f;
            if (held & HidNpadButton_AnyUp)    dpitch += 0.02f;
            if (held & HidNpadButton_AnyDown)  dpitch -= 0.02f;
            if (held & HidNpadButton_L)        dzoom  += 0.12f;
            if (held & HidNpadButton_R)        dzoom  -= 0.12f;

            scene_camera_orbit(&camera, dyaw, dpitch, dzoom);

            const Shelf *shelf = &lib.shelves.items[shelf_index];
            char subtitle[96];
            snprintf(subtitle, sizeof(subtitle), "%zu items", shelf->count);

            SDL_Rect viewport = ui_room_begin(&render, &lib.overrides.prefs);
            scene_draw(scene, &render, &camera, viewport,
                       (float)room_frames / 60.0f);
            ui_room_end(&render, &lib.overrides.prefs, shelf->name, subtitle,
                        "Placeholder model - franchises.json wiring is next");
            room_frames++;
            continue;
        }

        if ((down & HidNpadButton_B) && lib.shelves.count > 0) {
            if (!scene) {
                snprintf(status, sizeof(status), "3D unavailable on this build");
            } else {
                room_open = true;
                room_frames = 0;
                scene_camera_reset(&camera);
            }
            continue;
        }

        if (picker.open) {
                picker_preview(&picker, &render);

                const Shelf *shelf = &lib.shelves.items[shelf_index];
                const Entry *entry = &lib.list.items[shelf->items[shelf->cursor]];
                ui_draw_cover_picker(&render, &picker, entry,
                                     &lib.overrides.prefs);
            }
            continue;
        }

        if ((down & HidNpadButton_StickR) && lib.shelves.count > 0) {
            picker_open(&picker, &lib, shelf_index, status, sizeof(status));
            continue;
        }

        if (settings.open) {
            size_t total = ui_settings_count(&lib.shelves);

            if (down & (HidNpadButton_Minus | HidNpadButton_B)) {
                settings.open = false;
            } else if (down & HidNpadButton_AnyUp) {
                settings.row = settings.row ? settings.row - 1 : total - 1;
            } else if (down & HidNpadButton_AnyDown) {
                settings.row = (settings.row + 1) % total;
            } else if (down & HidNpadButton_A) {
                Prefs *prefs = &lib.overrides.prefs;

                if (settings.row >= Setting_Fixed) {
                    size_t index = settings.row - Setting_Fixed;
                    const char *shelf_name = NULL;

                    if (index < lib.shelves.count)
                        shelf_name = lib.shelves.items[index].name;
                    else if (index - lib.shelves.count < lib.shelves.hidden_count)
                        shelf_name = lib.shelves.hidden_names[index - lib.shelves.count];

                    if (shelf_name) {
                        /* Copy first: regrouping invalidates both arrays. */
                        char name[SHELF_NAME_LEN];
                        snprintf(name, sizeof(name), "%s", shelf_name);
                        overrides_toggle_shelf(&lib.overrides, name);
                        library_regroup(&lib);
                    }
                } else {
                    switch (settings.row) {
                        case Setting_Theme:
                            prefs->theme = (prefs->theme + 1) % ui_theme_count();
                            break;
                        case Setting_PosterTiles:
                            prefs->poster_tiles = !prefs->poster_tiles;
                            break;
                        case Setting_LargeTiles:
                            prefs->cover_size = (prefs->cover_size + 1) % 3;
                            break;
                        case Setting_ShowHidden:
                            prefs->show_hidden = !prefs->show_hidden;
                            library_regroup(&lib);
                            break;
                        case Setting_SgdbKey: {
                            char key[sizeof(prefs->sgdb_key)];
                            snprintf(key, sizeof(key), "%s", prefs->sgdb_key);

                            if (keyboard_prompt("SteamGridDB API key",
                                                "steamgriddb.com/profile/preferences/api",
                                                key, key, sizeof(key))) {
                                snprintf(prefs->sgdb_key, sizeof(prefs->sgdb_key),
                                         "%s", key);
                                snprintf(status, sizeof(status), "API key saved");
                            }
                            break;
                        }
                        case Setting_FetchCovers:
                            fetch_shelf_covers(&lib, shelf_index, &render, &settings,
                                               icons, core_note, status, sizeof(status));
                            break;
                        case Setting_UnhideAll:
                            overrides_unhide_all(&lib.overrides);
                            library_regroup(&lib);
                            break;
                        case Setting_Diagnostics: {
                            char shot[512] = { 0 };
                            bool shot_ok = diag_screenshot(render.renderer, SHOTS_DIR,
                                                           shot, sizeof(shot));
                            bool rep_ok = diag_write_report(REPORT_PATH, &lib.list,
                                                            &lib.systems, &lib.shelves,
                                                            &lib.overrides);
                            snprintf(status, sizeof(status),
                                     "diagnostics: report %s, screenshot %s",
                                     rep_ok ? "ok" : "failed",
                                     shot_ok ? "ok" : "failed");
                            break;
                        }
                        default:
                            rescan(&lib, status, sizeof(status));
                            break;
                    }
                }

                lib.overrides.dirty = true;
                mkdir(CONFIG_DIR, 0777);
                overrides_save(&lib.overrides, CONFIG_PATH);

                if (shelf_index >= lib.shelves.count)
                    shelf_index = lib.shelves.count ? lib.shelves.count - 1 : 0;
                ui_state_bump(&ui);
            }

            /* Hiding a shelf shortens the list under the cursor. */
            total = ui_settings_count(&lib.shelves);
            if (settings.row >= total)
                settings.row = total - 1;

            ui_draw_settings(&render, &settings, &lib.overrides.prefs, &lib.list,
                             &lib.shelves, &lib.overrides, core_note);
            continue;
        }

        if (down & HidNpadButton_StickL) {
            char shot[512];
            if (diag_screenshot(render.renderer, SHOTS_DIR, shot, sizeof(shot)))
                snprintf(status, sizeof(status), "saved %s", shot);
            else
                snprintf(status, sizeof(status), "screenshot failed");
        }

        if (down & HidNpadButton_Minus) {
            settings.open = true;
            settings.row = 0;
            continue;
        }

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
            lib.overrides.prefs.show_hidden = !lib.overrides.prefs.show_hidden;
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
            diag_logf("launch kind=%d \"%s\" path=%s", entry->kind, entry->name,
                      entry->path[0] ? entry->path : "(title)");

            Result rc = launch_entry(entry, &lib.systems);
            diag_logf("launch result 0x%x", rc);

            if (R_SUCCEEDED(rc))
                break;   /* hbloader chainloads, or the system takes over. */

            snprintf(status, sizeof(status), "launch failed (0x%x)", rc);
        }

        ui_draw(&render, icons, &lib.list, &lib.shelves, shelf_index, &ui,
                &lib.overrides, status);
    }

done:
    picker_close(&picker);
    scene_destroy(scene);

    if (lib.overrides.dirty)
        overrides_save(&lib.overrides, CONFIG_PATH);

    overrides_free(&lib.overrides);
    shelves_free(&lib.shelves);
    systems_free(&lib.systems);
    entry_list_free(&lib.list);
    icons_destroy(icons);
    render_exit(&render);
    net_exit();
    diag_logf("clean exit");
    diag_close();
    return 0;
}
