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

/*
 * Settings overlay. Every row is selectable; section headings are drawn between
 * them by index, which keeps navigation a simple increment.
 */
typedef enum {
    Setting_PosterTiles = 0,
    Setting_LargeTiles  = 1,
    Setting_ShowHidden  = 2,
    Setting_Rescan      = 3,
    Setting_UnhideAll   = 4,
    Setting_Count       = 5,
} SettingRow;

typedef struct {
    bool   open;
    size_t row;
} Settings;

void ui_draw_settings(Render *render, const Settings *settings, const Prefs *prefs,
                      const EntryList *list, const ShelfList *shelves,
                      const char *core_note);

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             ShelfList *shelves, size_t shelf_index, UiState *state,
             const OverrideList *overrides, const char *status);

/*
 * Full-screen explainer shown when started in applet mode. Emulator cores and
 * high-resolution artwork do not fit in the ~56MB hbloader grants there, so the
 * app refuses to run rather than crashing later.
 */
void ui_draw_mode_gate(Render *render);
