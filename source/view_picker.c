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

    for (size_t i = 0; i < SGDB_MAX_COVERS; i++) {
        if (picker->thumbs[i]) {
            SDL_DestroyTexture(picker->thumbs[i]);
            picker->thumbs[i] = NULL;
        }
    }

    picker->open = false;
    picker->covers.count = 0;
    picker->index = 0;
    picker->loaded = 0;
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
    picker->loaded = 0;

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

/*
 * One thumbnail per frame. Fetching all of them before showing anything would
 * stall the picker for seconds; this way the grid appears immediately and fills
 * in, and the highlighted cell is fetched first so it is never the last to
 * arrive.
 */
static void load_next_thumb(App *app)
{
    CoverPicker *picker = &app->picker;

    if (picker->loaded >= picker->covers.count)
        return;

    size_t target = picker->index;
    if (picker->thumbs[target]) {
        target = picker->covers.count;
        for (size_t i = 0; i < picker->covers.count; i++)
            if (!picker->thumbs[i]) {
                target = i;
                break;
            }
        if (target == picker->covers.count)
            return;
    }

    char temp[512];
    snprintf(temp, sizeof(temp), "%s/.thumb.png", COVERS_DIR);
    mkdir(COVERS_DIR, 0777);

    if (net_download(picker->covers.items[target].thumb, temp))
        picker->thumbs[target] = IMG_LoadTexture(app->render->renderer, temp);

    /* Counted either way, so a broken thumbnail cannot wedge the loop. */
    picker->loaded++;
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
    overrides_set_cover(&app->lib.overrides, entry,
                        app->lib.overrides.prefs.poster_tiles, url);
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
        /* Grid navigation: five across, matching the drawn layout. */
        enum { COLS = 5 };

        if (down & HidNpadButton_AnyLeft)
            picker->index = picker->index ? picker->index - 1
                                          : picker->covers.count - 1;
        if (down & HidNpadButton_AnyRight)
            picker->index = (picker->index + 1) % picker->covers.count;
        if ((down & HidNpadButton_AnyUp) && picker->index >= COLS)
            picker->index -= COLS;
        if ((down & HidNpadButton_AnyDown) && picker->index + COLS < picker->covers.count)
            picker->index += COLS;

        if (down & HidNpadButton_A)
            pin_selected(app);
    }

    /* pin_selected may have closed the picker on success. */
    if (picker->open) {
        const Entry *entry = app_entry(app);
        load_next_thumb(app);

        if (entry)
            ui_draw_cover_picker(app->render, picker, entry,
                                 &app->lib.overrides.prefs);
    }

    return true;
}
