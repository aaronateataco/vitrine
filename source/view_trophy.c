#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diag.h"
#include "net.h"

void view_trophy_close(App *app)
{
    TrophyRoom *room = &app->trophies;

    for (size_t i = 0; i < RA_MAX_TROPHIES; i++) {
        if (room->textures[i]) {
            scene_free_texture(room->textures[i]);
            room->textures[i] = 0;
        }
    }

    room->open = false;
    room->trophies.count = 0;
    room->focus = 0;
    room->frames = 0;
    room->message[0] = '\0';
}

/*
 * Every failure here is reported inside the room rather than blocking it, so
 * the user sees why it is empty instead of a blank screen.
 */
void view_trophy_open(App *app)
{
    const Prefs *prefs = &app->lib.overrides.prefs;
    TrophyRoom *room = &app->trophies;

    view_trophy_close(app);
    room->open = true;

    scene_camera_reset(&app->camera);
    app->camera.distance = 7.5f;

    if (!prefs->ra_user[0] || !prefs->ra_key[0]) {
        snprintf(room->message, sizeof(room->message),
                 "Set a RetroAchievements user and key in Settings");
        return;
    }

    if (!net_init()) {
        snprintf(room->message, sizeof(room->message), "Network unavailable");
        return;
    }

    if (!ra_fetch_recent(prefs->ra_user, prefs->ra_key, TROPHY_WINDOW_MINUTES,
                         &room->trophies)) {
        snprintf(room->message, sizeof(room->message),
                 "Could not reach RetroAchievements");
        return;
    }

    if (room->trophies.count == 0) {
        snprintf(room->message, sizeof(room->message),
                 "No achievements unlocked in the last 30 days");
        return;
    }

    for (size_t i = 0; i < room->trophies.count; i++) {
        if (!ra_ensure_badge(room->trophies.items[i].badge, BADGES_DIR))
            continue;

        char path[512];
        ra_badge_path(room->trophies.items[i].badge, BADGES_DIR, path, sizeof(path));
        room->textures[i] = scene_load_texture(path);
    }

    diag_logf("trophies: %zu loaded", room->trophies.count);
}

bool view_trophy_update(App *app, u64 down)
{
    TrophyRoom *room = &app->trophies;

    if (!room->open)
        return false;

    if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        view_trophy_close(app);
        return true;
    }

    if (room->trophies.count > 0) {
        if (down & HidNpadButton_AnyLeft)
            room->focus = room->focus ? room->focus - 1 : room->trophies.count - 1;
        if (down & HidNpadButton_AnyRight)
            room->focus = (room->focus + 1) % room->trophies.count;

        /* Held, not pressed: zooming wants continuous input. */
        u64 held = padGetButtons(app->pad);
        float dzoom = 0.0f;
        if (held & HidNpadButton_L) dzoom += 0.12f;
        if (held & HidNpadButton_R) dzoom -= 0.12f;
        scene_camera_orbit(&app->camera, 0.0f, 0.0f, dzoom);
    }

    SDL_Rect viewport = ui_room_begin(app->render, &app->lib.overrides.prefs);

    if (room->trophies.count > 0) {
        /* Slide the camera so the focused medal stays centred. */
        app->camera.target[0] =
            ((float)room->focus - ((float)room->trophies.count - 1.0f) * 0.5f) * 2.8f;

        scene_draw_medals(app->scene, app->render, &app->camera, viewport,
                          (float)room->frames / 60.0f, room->textures,
                          room->trophies.count, room->focus);
    }

    const RaTrophy *focused = room->trophies.count
                                  ? &room->trophies.items[room->focus]
                                  : NULL;

    char subtitle[128];
    if (focused)
        snprintf(subtitle, sizeof(subtitle), "%zu of %zu   %d points",
                 room->focus + 1, room->trophies.count, focused->points);
    else
        subtitle[0] = '\0';

    ui_room_end(app->render, &app->lib.overrides.prefs,
                focused ? focused->title : "Trophy Room", subtitle,
                focused ? focused->game : room->message);

    room->frames++;
    return true;
}
