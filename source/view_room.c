#include <stdio.h>
#include <string.h>

#include "app.h"

void view_room_open(App *app)
{
    const Shelf *shelf = app_shelf(app);

    if (!app->scene) {
        app_status(app, "3D unavailable on this build");
        return;
    }
    if (!shelf)
        return;

    app->room.open = true;
    app->room.frames = 0;

    scene_camera_reset(&app->camera);
    scene_clear_model(app->scene);

    const Room *room = rooms_find(&app->rooms, shelf->name);
    if (!room)
        return;

    app->camera.yaw = room->camera.yaw;
    app->camera.pitch = room->camera.pitch;
    app->camera.distance = room->camera.distance;
    memcpy(app->camera.target, room->camera.target, sizeof(app->camera.target));

    /* A missing or broken model is not fatal: the placeholder stays and the
       room still opens, with the path reported inside it. */
    if (room->model[0] && !scene_load_model(app->scene, room->model))
        app_status(app, "could not load %s", room->model);
}

/* Attribution for a model the user supplied themselves. */
static void describe_source(const App *app, const Room *room, char *out, size_t size)
{
    if (room && room->model[0] && !scene_has_model(app->scene))
        snprintf(out, size, "Model not loaded: %s", room->model);
    else if (room && room->credit[0] && room->source[0])
        snprintf(out, size, "%s  -  %s", room->credit, room->source);
    else if (room && room->credit[0])
        snprintf(out, size, "%s", room->credit);
    else if (room && room->source[0])
        snprintf(out, size, "%s", room->source);
    else
        snprintf(out, size, "No room defined for this shelf - see franchises.json");
}

bool view_room_update(App *app, u64 down)
{
    if (!app->room.open)
        return false;

    if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        app->room.open = false;
        return true;
    }

    const Shelf *shelf = app_shelf(app);
    if (!shelf) {
        app->room.open = false;
        return true;
    }

    /* Held, not pressed: orbiting wants continuous input. */
    u64 held = padGetButtons(app->pad);
    float dyaw = 0.0f;
    float dpitch = 0.0f;
    float dzoom = 0.0f;

    if (held & HidNpadButton_AnyLeft)  dyaw   -= 0.03f;
    if (held & HidNpadButton_AnyRight) dyaw   += 0.03f;
    if (held & HidNpadButton_AnyUp)    dpitch += 0.02f;
    if (held & HidNpadButton_AnyDown)  dpitch -= 0.02f;
    if (held & HidNpadButton_L)        dzoom  += 0.12f;
    if (held & HidNpadButton_R)        dzoom  -= 0.12f;

    scene_camera_orbit(&app->camera, dyaw, dpitch, dzoom);

    const Room *room = rooms_find(&app->rooms, shelf->name);

    char subtitle[96];
    snprintf(subtitle, sizeof(subtitle), "%zu items", shelf->count);

    char credit[320];
    describe_source(app, room, credit, sizeof(credit));

    SDL_Rect viewport = ui_room_begin(app->render, &app->lib.overrides.prefs);
    scene_draw(app->scene, app->render, &app->camera, viewport,
               (float)app->room.frames / 60.0f);
    ui_room_end(app->render, &app->lib.overrides.prefs,
                room && room->title[0] ? room->title : shelf->name,
                subtitle, credit);

    app->room.frames++;
    return true;
}
