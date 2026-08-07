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
             const char *status);
