#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diag.h"
#include "keyboard.h"
#include "net.h"
#include "sgdb.h"

/*
 * Blocks on the network, so it repaints after each entry with a running count
 * instead of appearing frozen.
 */
void view_settings_fetch_covers(App *app)
{
    const Prefs *prefs = &app->lib.overrides.prefs;
    const Shelf *shelf = app_shelf(app);

    if (!prefs->sgdb_key[0]) {
        app_status(app, "set a SteamGridDB API key first");
        return;
    }
    if (!shelf) {
        app_status(app, "no shelf selected");
        return;
    }
    if (!net_init()) {
        app_status(app, "network unavailable");
        return;
    }

    size_t got = 0;
    size_t skipped = 0;

    for (size_t i = 0; i < shelf->count; i++) {
        const Entry *entry = &app->lib.list.items[shelf->items[i]];

        /* Already cached: leave it alone so repeat runs are cheap. */
        if (sgdb_cached(entry, prefs->poster_tiles, COVERS_DIR)) {
            skipped++;
            continue;
        }

        /* Each of these is a network round trip; without pumping, the console
           decides the app has hung. */
        if (!app_pump(app))
            break;

        app_progress(app, "Downloading covers", entry->name,
                     (float)i / (float)shelf->count);

        if (sgdb_fetch_for_entry(prefs->sgdb_key, entry, prefs->poster_tiles,
                                 COVERS_DIR,
                                 overrides_cover(&app->lib.overrides, entry,
                                                 prefs->poster_tiles),
                                 NULL, 0))
            got++;
    }

    icons_flush(app->icons);
    app_status(app, "covers: %zu downloaded, %zu already cached", got, skipped);
    diag_logf("cover fetch for \"%s\": %zu new, %zu cached", shelf->name, got, skipped);
}

static void prompt_into(App *app, const char *header, const char *guide,
                        char *field, size_t field_size, const char *saved_message)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", field);

    if (!keyboard_prompt(header, guide, buffer, buffer, sizeof(buffer)))
        return;   /* Cancelled. */

    snprintf(field, field_size, "%s", buffer);
    if (saved_message)
        app_status(app, "%s", saved_message);
}

/* Rows past the fixed block are shelf visibility toggles, visible ones first
   then those hidden out of the library. */
static void toggle_shelf_row(App *app, size_t index)
{
    const ShelfList *shelves = &app->lib.shelves;
    const char *name = NULL;

    if (index < shelves->count)
        name = shelves->items[index].name;
    else if (index - shelves->count < shelves->hidden_count)
        name = shelves->hidden_names[index - shelves->count];

    if (!name)
        return;

    /* Copy first: regrouping invalidates both arrays. */
    char copy[SHELF_NAME_LEN];
    snprintf(copy, sizeof(copy), "%s", name);

    overrides_toggle_shelf(&app->lib.overrides, copy);
    app_regroup(app);
}

static void activate(App *app)
{
    Prefs *prefs = &app->lib.overrides.prefs;

    if (app->settings.row >= Setting_Fixed) {
        toggle_shelf_row(app, app->settings.row - Setting_Fixed);
        return;
    }

    switch (app->settings.row) {
        case Setting_Theme:
            prefs->theme = (prefs->theme + 1) % ui_theme_count();
            break;

        case Setting_Layout:
            prefs->layout = (prefs->layout + 1) % 2;
            break;

        case Setting_PosterTiles:
            prefs->poster_tiles = !prefs->poster_tiles;
            break;

        case Setting_LargeTiles:
            prefs->cover_size = (prefs->cover_size + 1) % 3;
            break;

        case Setting_ShowHidden:
            prefs->show_hidden = !prefs->show_hidden;
            app_regroup(app);
            break;

        case Setting_SgdbKey:
            prompt_into(app, "SteamGridDB API key",
                        "steamgriddb.com/profile/preferences/api",
                        prefs->sgdb_key, sizeof(prefs->sgdb_key), "API key saved");
            break;

        case Setting_FetchCovers:
            view_settings_fetch_covers(app);
            break;

        case Setting_RaUser:
            prompt_into(app, "RetroAchievements username", NULL,
                        prefs->ra_user, sizeof(prefs->ra_user), NULL);
            break;

        case Setting_RaKey:
            prompt_into(app, "RetroAchievements web API key",
                        "retroachievements.org/controlpanel.php",
                        prefs->ra_key, sizeof(prefs->ra_key), NULL);
            break;

        case Setting_TrophyRoom:
            app->settings.open = false;
            view_trophy_open(app);
            break;

        case Setting_UnhideAll:
            overrides_unhide_all(&app->lib.overrides);
            app_regroup(app);
            break;

        case Setting_Diagnostics: {
            char shot[512] = { 0 };
            bool shot_ok = diag_screenshot(app->render->renderer, SHOTS_DIR,
                                           shot, sizeof(shot));
            bool report_ok = diag_write_report(REPORT_PATH, &app->lib.list,
                                               &app->lib.systems, &app->lib.shelves,
                                               &app->lib.overrides);
            app_status(app, "diagnostics: report %s, screenshot %s",
                       report_ok ? "ok" : "failed", shot_ok ? "ok" : "failed");
            break;
        }

        default:
            app_rescan(app);
            app_note_missing_cores(app);
            break;
    }
}

bool view_settings_update(App *app, u64 down)
{
    if (!app->settings.open)
        return false;

    size_t total = ui_settings_count(&app->lib.shelves);

    if (down & (HidNpadButton_Minus | HidNpadButton_B)) {
        app->settings.open = false;
    } else if (down & HidNpadButton_AnyUp) {
        app->settings.row = app->settings.row ? app->settings.row - 1 : total - 1;
    } else if (down & HidNpadButton_AnyDown) {
        app->settings.row = (app->settings.row + 1) % total;
    } else if (down & HidNpadButton_A) {
        activate(app);

        app->lib.overrides.dirty = true;
        app_save_config(app);
        app_clamp_shelf(app);
        ui_state_bump(&app->ui);
    }

    /* Closed this frame (by B, or by opening the Trophy Room). Consume it and
       let the next frame draw whatever is now active. */
    if (!app->settings.open)
        return true;

    /* Hiding a shelf shortens the list under the cursor. */
    total = ui_settings_count(&app->lib.shelves);
    if (app->settings.row >= total)
        app->settings.row = total - 1;

    ui_draw_settings(app->render, &app->settings, &app->lib.overrides.prefs,
                     &app->lib.list, &app->lib.shelves, &app->lib.overrides,
                     app->core_note);
    return true;
}
