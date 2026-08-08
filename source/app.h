#pragma once

#include "config.h"
#include "icons.h"
#include "overrides.h"
#include "ra.h"
#include "render.h"
#include "rooms.h"
#include "scene.h"
#include "shelves.h"
#include "ui.h"

/* Everything VITRINE writes lives under one directory, so a session can be
   copied off the card whole. */
#define HOMEBREW_ROOT "sdmc:/switch"
#define CONFIG_DIR    "sdmc:/switch/vitrine"
#define SYSTEMS_PATH  CONFIG_DIR "/systems.ini"
#define CONFIG_PATH   CONFIG_DIR "/config.json"
#define LOG_PATH      CONFIG_DIR "/vitrine.log"
#define REPORT_PATH   CONFIG_DIR "/diagnostics.txt"
#define SHOTS_DIR     CONFIG_DIR "/screenshots"
#define COVERS_DIR    CONFIG_DIR "/covers"
#define ROOMS_PATH    CONFIG_DIR "/franchises.json"
#define BADGES_DIR    CONFIG_DIR "/badges"

#define PAGE_JUMP             6
#define TROPHY_WINDOW_MINUTES 43200   /* ~30 days of unlocks */

typedef struct {
    EntryList    list;
    SystemList   systems;
    ShelfList    shelves;
    OverrideList overrides;   /* also carries the display prefs */
} Library;

/* Trophy Room state. Badges become GL textures; a medal without one still
   renders as a plain metal coin rather than vanishing. */
typedef struct {
    bool         open;
    RaTrophyList trophies;
    unsigned     textures[RA_MAX_TROPHIES];
    size_t       focus;
    u64          frames;
    char         message[160];
} TrophyRoom;

typedef struct {
    bool open;
    u64  frames;
} RoomView;

/*
 * One bag of state passed to every view, so each modal screen lives in its own
 * file and the main loop stays a dispatcher. The views previously sat inline in
 * main(), where a scripted edit once pasted an entire handler twice without
 * anything catching it.
 */
typedef struct {
    Render      *render;
    PadState    *pad;
    IconCache   *icons;
    Scene       *scene;

    Library      lib;
    RoomList     rooms;
    SceneCamera  camera;

    Settings     settings;
    CoverPicker  picker;
    TrophyRoom   trophies;
    RoomView     room;

    size_t       shelf_index;
    UiState      ui;
    char         status[256];
    char         core_note[256];
} App;

void app_regroup(App *app);
void app_rescan(App *app);
void app_save_config(App *app);
void app_clamp_shelf(App *app);
void app_note_missing_cores(App *app);
void app_status(App *app, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* NULL when the library is empty, which every caller must handle. */
const Shelf *app_shelf(const App *app);
const Entry *app_entry(const App *app);

/*
 * Each returns true when it consumed the frame. main() tries them in priority
 * order and falls through to library navigation.
 */
bool view_trophy_update(App *app, u64 down);
bool view_room_update(App *app, u64 down);
bool view_picker_update(App *app, u64 down);
bool view_settings_update(App *app, u64 down);

void view_trophy_open(App *app);
void view_trophy_close(App *app);
void view_room_open(App *app);
void view_picker_open(App *app);
void view_picker_close(App *app);
void view_settings_fetch_covers(App *app);
