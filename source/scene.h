#pragma once

#include <stdbool.h>

#include "render.h"

/*
 * Minimal GLES2 layer for Game Rooms.
 *
 * SDL2's renderer already owns a GLES context on this platform, so rather than
 * creating a second one, 3D work is issued into the same context between
 * SDL_RenderFlush calls. That keeps the 2D UI and the room in one window at the
 * cost of having to restore the small amount of state SDL cares about.
 *
 * This is the foundation: a lit placeholder solid with an orbit camera. Model
 * loading comes next, and hangs off the same draw path.
 */

typedef struct {
    float yaw;       ///< Radians, orbit around the target.
    float pitch;     ///< Radians, clamped away from the poles.
    float distance;  ///< Camera distance from the target.
    float target[3];
} SceneCamera;

typedef struct Scene Scene;

/// Returns NULL when shaders or buffers cannot be created; 3D is then skipped.
Scene *scene_create(void);
void   scene_destroy(Scene *scene);

void scene_camera_reset(SceneCamera *camera);

/*
 * Nudges the orbit. Values are per-frame deltas, typically taken from a stick.
 */
void scene_camera_orbit(SceneCamera *camera, float dyaw, float dpitch, float dzoom);

/*
 * Draws into `viewport` (screen coordinates, y down). Wraps its own
 * SDL_RenderFlush, so callers may mix it freely with 2D drawing.
 */
void scene_draw(Scene *scene, Render *render, const SceneCamera *camera,
                SDL_Rect viewport, float seconds);
