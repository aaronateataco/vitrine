#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL_image.h>

#include "app.h"
#include "net.h"
#include "sgdb.h"

void view_picker_close(App *app)
{
    CoverPicker *picker = &app->picker;

    if (picker->preview) {
        SDL_DestroyTexture(picker->preview);
        picker->preview = NULL;
    }

    picker->open = false;
    picker->covers.count = 0;
    picker->index = 0;
    picker->message[0] = '\0';
}

void view_picker_open(App *app)
{
    const Prefs *prefs = &app->lib.overrides.prefs;
    const Entry *entry = app_entry(app);
    CoverPicker *picker = &app->picker;

    view_picker_close(app);

    if (!entry)
        return;
    if (!prefs->sgdb_key[0]) {
        app_status(app, "set a SteamGridDB API key first");
        return;
    }
    if (!net_init()) {
        app_status(app, "network unavailable");
        return;
    }

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

/* Fetched only for the highlighted row: downloading every candidate up front
   would make opening the list slow for artwork nobody looks at. */
static void refresh_preview(App *app)
{
    CoverPicker *picker = &app->picker;

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
        picker->preview = IMG_LoadTexture(app->render->renderer, temp);

    picker->preview_index = picker->index;
}

static void pin_selected(App *app)
{
    const Entry *entry = app_entry(app);
    CoverPicker *picker = &app->picker;

    if (!entry)
        return;

    const char *url = picker->covers.items[picker->index].url;

    /* Pin first, then fetch through the normal path so the cache filename
       matches what the library will later look for. */
    overrides_set_cover(&app->lib.overrides, entry, url);
    app_save_config(app);

    if (sgdb_fetch_for_entry(app->lib.overrides.prefs.sgdb_key, entry,
                             app->lib.overrides.prefs.poster_tiles,
                             COVERS_DIR, url, NULL, 0)) {
        icons_flush(app->icons);
        app_status(app, "cover locked for %s", entry->name);
        view_picker_close(app);
    } else {
        snprintf(picker->message, sizeof(picker->message), "download failed");
    }
}

bool view_picker_update(App *app, u64 down)
{
    CoverPicker *picker = &app->picker;

    if (!picker->open)
        return false;

    if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        view_picker_close(app);
        return true;
    }

    if (picker->covers.count > 0) {
        if (down & HidNpadButton_AnyUp)
            picker->index = picker->index ? picker->index - 1
                                          : picker->covers.count - 1;
        if (down & HidNpadButton_AnyDown)
            picker->index = (picker->index + 1) % picker->covers.count;

        if (down & HidNpadButton_A)
            pin_selected(app);
    }

    /* pin_selected may have closed the picker on success. */
    if (picker->open) {
        const Entry *entry = app_entry(app);
        refresh_preview(app);

        if (entry)
            ui_draw_cover_picker(app->render, picker, entry,
                                 &app->lib.overrides.prefs);
    }

    return true;
}
