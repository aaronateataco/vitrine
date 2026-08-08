#pragma once

#include "entry.h"
#include "icons.h"
#include "render.h"
#include "shelves.h"

/// Animation state. Targets are recomputed each frame and eased toward.
typedef struct {
    float scroll_y;   ///< Vertical offset of the shelf stack, in pixels.
    float pulse;      ///< 0..1, restarts on selection change.
} UiState;

void ui_state_init(UiState *state);

/// Call whenever the selection moves, so the cursor animation restarts.
void ui_state_bump(UiState *state);

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             ShelfList *shelves, size_t shelf_index, UiState *state,
             const OverrideList *overrides, bool show_hidden, const char *status);

/*
 * Full-screen explainer shown when started in applet mode. Emulator cores and
 * high-resolution artwork do not fit in the ~56MB hbloader grants there, so the
 * app refuses to run rather than crashing later.
 */
void ui_draw_mode_gate(Render *render);
