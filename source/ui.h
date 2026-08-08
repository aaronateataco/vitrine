#pragma once

#include "entry.h"
#include "icons.h"
#include "render.h"
#include "sgdb.h"
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
 * Settings rows. The fixed rows come first; every shelf then contributes one
 * visibility toggle, so the count is dynamic. Section headings are drawn
 * between rows by index, which keeps navigation a simple increment.
 */
typedef enum {
    Setting_Theme       = 0,
    Setting_PosterTiles = 1,
    Setting_LargeTiles  = 2,
    Setting_ShowHidden  = 3,
    Setting_SgdbKey     = 4,
    Setting_FetchCovers = 5,
    Setting_Rescan      = 6,
    Setting_UnhideAll   = 7,
    Setting_Diagnostics = 8,
    Setting_Fixed       = 9,
} SettingRow;

typedef struct {
    bool   open;
    size_t row;
} Settings;

/// Fixed rows plus one per shelf.
size_t      ui_settings_count(const ShelfList *shelves);
int         ui_theme_count(void);
const char *ui_theme_name(int index);

/*
 * Cover picker. Candidates are listed with their uploader; the highlighted one
 * is previewed, downloaded on demand so opening the list stays fast.
 */
typedef struct {
    bool          open;
    size_t        index;
    SgdbCoverList covers;
    SDL_Texture  *preview;
    size_t        preview_index;
    char          message[128];
} CoverPicker;

void ui_draw_cover_picker(Render *render, const CoverPicker *picker,
                          const Entry *entry, const Prefs *prefs);

void ui_draw_settings(Render *render, const Settings *settings, const Prefs *prefs,
                      const EntryList *list, const ShelfList *shelves,
                      const OverrideList *overrides, const char *core_note);

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             ShelfList *shelves, size_t shelf_index, UiState *state,
             const OverrideList *overrides, const char *status);

/*
 * Full-screen explainer shown when started in applet mode. Emulator cores and
 * high-resolution artwork do not fit in the ~56MB hbloader grants there, so the
 * app refuses to run rather than crashing later.
 */
void ui_draw_mode_gate(Render *render);
