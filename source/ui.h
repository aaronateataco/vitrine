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
    Setting_Layout      = 1,
    Setting_PosterTiles = 2,
    Setting_LargeTiles  = 3,
    Setting_ShowHidden  = 4,
    Setting_SgdbKey     = 5,
    Setting_FetchCovers = 6,
    Setting_RaUser      = 7,
    Setting_RaKey       = 8,
    Setting_TrophyRoom  = 9,
    Setting_Rescan      = 10,
    Setting_UnhideAll   = 11,
    Setting_Diagnostics = 12,
    Setting_Fixed       = 13,
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

/*
 * Game Room. Split in two so the 3D pass can sit between them: begin paints the
 * background and returns the viewport the scene should fill, end lays the 2D
 * chrome over the top and presents.
 */
SDL_Rect ui_room_begin(Render *render, const Prefs *prefs);
void     ui_room_end(Render *render, const Prefs *prefs, const char *title,
                     const char *subtitle, const char *source);
