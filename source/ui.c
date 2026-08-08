#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

#define MARGIN_X     64
#define HEADER_H     76
#define FOOTER_H    108
#define TILE_GAP     18
#define SHELF_LABEL  30
#define SHELF_PAD    26
#define VIEW_TOP     HEADER_H
#define VIEW_BOTTOM (SCREEN_H - FOOTER_H)
#define SELECT_SCALE 1.10f

static const SDL_Color COL_BG      = {  14,  16,  20, 255 };
static const SDL_Color COL_PANEL   = {  26,  30,  38, 255 };
static const SDL_Color COL_RAISED  = {  38,  46,  62, 255 };
static const SDL_Color COL_LINE    = {  52,  58,  74, 255 };
static const SDL_Color COL_TEXT    = { 240, 242, 246, 255 };
static const SDL_Color COL_DIM     = { 138, 144, 160, 255 };
static const SDL_Color COL_FAINT   = {  74,  80,  96, 255 };
static const SDL_Color COL_ACCENT  = {  90, 169, 255, 255 };
static const SDL_Color COL_WARN    = { 236, 130, 130, 255 };

/*
 * Tile geometry follows the display preferences. Posters use the 2:3 ratio that
 * SteamGridDB grids ship in; square matches the Switch's own 1:1 icons.
 */
static int tile_w(const Prefs *prefs)
{
    if (prefs->poster_tiles)
        return prefs->large_tiles ? 148 : 116;
    return prefs->large_tiles ? 168 : 132;
}

static int tile_h(const Prefs *prefs)
{
    int w = tile_w(prefs);
    return prefs->poster_tiles ? w * 3 / 2 : w;
}

static int shelf_h(const Prefs *prefs)
{
    return SHELF_LABEL + tile_h(prefs) + SHELF_PAD;
}

void ui_state_init(UiState *state)
{
    state->scroll_y = 0.0f;
    state->pulse = 1.0f;
}

void ui_state_bump(UiState *state)
{
    state->pulse = 0.0f;
}

/* Exponential ease. Frame-rate dependent, but vsync pins us at 60Hz. */
static float approach(float current, float target, float rate)
{
    float delta = target - current;
    if (fabsf(delta) < 0.5f)
        return target;
    return current + delta * rate;
}

/* Stable per-platform tint, so ROM plates are recognisable at a glance. */
static SDL_Color plate_color(const char *seed)
{
    unsigned hash = 2166136261u;
    for (const char *p = seed; *p; p++)
        hash = (hash ^ (unsigned char)*p) * 16777619u;

    SDL_Color color;
    color.r = (Uint8)(46 + (hash         & 0x3f));
    color.g = (Uint8)(52 + ((hash >> 9)  & 0x3f));
    color.b = (Uint8)(72 + ((hash >> 18) & 0x4f));
    color.a = 255;
    return color;
}

static void draw_tile(Render *render, IconCache *icons, const Entry *entry,
                      size_t entry_index, int x, int y, bool selected, float pulse,
                      bool hidden, const Prefs *prefs)
{
    int w = tile_w(prefs);
    int h = tile_h(prefs);
    int ox = x;
    int oy = y;

    if (selected) {
        /* Grow from the centre so neighbours are not pushed visually. */
        float scale = 1.0f + (SELECT_SCALE - 1.0f) * pulse;
        int sw = (int)(w * scale);
        int sh = (int)(h * scale);
        ox = x - (sw - w) / 2;
        oy = y - (sh - h) / 2;
        w = sw;
        h = sh;
    }

    SDL_Rect rect = { ox, oy, w, h };

    if (selected)
        render_shadow(render, rect, 10);

    SDL_Texture *texture = icons_get(icons, entry, entry_index);

    if (texture) {
        SDL_RenderCopy(render->renderer, texture, NULL, &rect);
    } else {
        const char *label = entry->author[0] ? entry->author : entry->name;
        render_fill(render, rect, plate_color(label));
        render_text_fit(render, render->font, rect.x, rect.y + h / 2 - 14, w,
                        COL_TEXT, label);
    }

    if (hidden) {
        /* Only visible in "show hidden" mode, so it must read as excluded. */
        SDL_Color veil = { 14, 16, 20, 170 };
        render_fill(render, rect, veil);
        render_text_fit(render, render->font, rect.x, rect.y + 6, w, COL_WARN,
                        "hidden");
    }

    if (selected)
        render_outline(render, rect, 3, COL_ACCENT);
    else if (!hidden)
        render_fill(render, rect, (SDL_Color){ 14, 16, 20, 110 });
}

static void draw_shelf(Render *render, IconCache *icons, const EntryList *list,
                       Shelf *shelf, int y, bool active, float pulse,
                       const OverrideList *overrides, const Prefs *prefs)
{
    char meta[64];
    int w = tile_w(prefs);
    int h = tile_h(prefs);
    int pitch = w + TILE_GAP;

    render_text(render, render->font, MARGIN_X, y, active ? COL_TEXT : COL_DIM,
                shelf->name);

    snprintf(meta, sizeof(meta), "%zu", shelf->count);
    render_text_right(render, render->font, SCREEN_W - MARGIN_X, y + 3, COL_FAINT, meta);

    if (active)
        render_fill(render, (SDL_Rect){ MARGIN_X - 14, y + 2, 4, 20 }, COL_ACCENT);

    int strip_y = y + SHELF_LABEL;
    int visible_w = SCREEN_W - 2 * MARGIN_X;

    /* Keep the cursor centred, but never scroll past either end. */
    float total_w = (float)shelf->count * pitch - TILE_GAP;
    float target = (float)shelf->cursor * pitch + w / 2.0f - visible_w / 2.0f;
    float max_scroll = total_w > visible_w ? total_w - visible_w : 0.0f;

    if (target < 0.0f)       target = 0.0f;
    if (target > max_scroll) target = max_scroll;

    shelf->scroll_x = approach(shelf->scroll_x, target, 0.22f);

    SDL_Rect clip = { MARGIN_X - 12, strip_y - 12, visible_w + 24, h + 24 };
    SDL_RenderSetClipRect(render->renderer, &clip);

    for (size_t i = 0; i < shelf->count; i++) {
        int x = MARGIN_X + (int)(i * pitch - shelf->scroll_x);
        if (x + w < MARGIN_X - 24 || x > SCREEN_W)
            continue;   /* Off-screen: skip the icon decode entirely. */

        const Entry *entry = &list->items[shelf->items[i]];
        bool hidden = overrides && overrides_hidden(overrides, entry);
        draw_tile(render, icons, entry, shelf->items[i], x, strip_y,
                  active && i == shelf->cursor, pulse, hidden, prefs);
    }

    SDL_RenderSetClipRect(render->renderer, NULL);

    /* Fade the strip into the margins so clipped tiles do not end abruptly. */
    SDL_Color edge = COL_BG;
    render_edge_fade(render, (SDL_Rect){ 0, strip_y - 12, MARGIN_X - 12, h + 24 },
                     edge, false);
    render_edge_fade(render, (SDL_Rect){ SCREEN_W - MARGIN_X + 12, strip_y - 12,
                                         MARGIN_X - 12, h + 24 }, edge, true);
}

static void draw_header(Render *render, const EntryList *list, const ShelfList *shelves)
{
    char line[128];

    render_text(render, render->font_large, MARGIN_X, 22, COL_TEXT, "VITRINE");

    snprintf(line, sizeof(line), "%zu items across %zu shelves",
             list->count, shelves->count);
    render_text(render, render->font, MARGIN_X + 190, 30, COL_FAINT, line);

    render_fill(render, (SDL_Rect){ 0, HEADER_H - 2, SCREEN_W, 1 }, COL_PANEL);
}

static void draw_footer(Render *render, const EntryList *list,
                        const ShelfList *shelves, size_t shelf_index,
                        const char *status)
{
    char line[256];
    int y = VIEW_BOTTOM + 16;

    render_fill(render, (SDL_Rect){ 0, VIEW_BOTTOM, SCREEN_W, FOOTER_H }, COL_PANEL);

    if (shelves->count == 0) {
        render_text(render, render->font_large, MARGIN_X, y, COL_DIM, "Nothing found");
        render_text(render, render->font, MARGIN_X, y + 40, COL_FAINT,
                    "Check systems.ini, then press Y to rescan");
        return;
    }

    const Shelf *shelf = &shelves->items[shelf_index];
    const Entry *entry = &list->items[shelf->items[shelf->cursor]];

    render_text(render, render->font_large, MARGIN_X, y, COL_TEXT, entry->name);

    const char *kind = entry->kind == EntryKind_Homebrew ? "Homebrew"
                     : entry->kind == EntryKind_Game     ? "ROM"
                                                         : "Installed";
    if (entry->author[0])
        snprintf(line, sizeof(line), "%s   %s   %zu of %zu",
                 entry->author, kind, shelf->cursor + 1, shelf->count);
    else
        snprintf(line, sizeof(line), "%s   %zu of %zu",
                 kind, shelf->cursor + 1, shelf->count);

    render_text(render, render->font, MARGIN_X, y + 42, COL_DIM, line);

    /* Right-aligned from a measured width, so it can never run off screen. */
    render_text_right(render, render->font, SCREEN_W - MARGIN_X, y + 42, COL_FAINT,
                      "A  play        -  settings");

    if (status && status[0])
        render_text(render, render->font, MARGIN_X, y + 70, COL_WARN, status);
}

void ui_draw_settings(Render *render, const Settings *settings, const Prefs *prefs,
                      const EntryList *list, const ShelfList *shelves,
                      const char *core_note)
{
    static const char *controls[][2] = {
        { "D-pad left / right", "Move within a shelf" },
        { "D-pad up / down",    "Change shelf" },
        { "L / R",              "Page within a shelf" },
        { "A",                  "Launch" },
        { "X",                  "Hide or unhide" },
        { "ZL",                 "Move homebrew to Installed Games" },
        { "ZR",                 "Reveal hidden items" },
        { "Y",                  "Rescan library" },
        { "+",                  "Exit" },
    };

    struct { SettingRow row; const char *label; const char *value; } items[Setting_Count] = {
        { Setting_PosterTiles, "Cover shape",       prefs->poster_tiles ? "Poster 2:3" : "Square 1:1" },
        { Setting_LargeTiles,  "Cover size",        prefs->large_tiles ? "Large" : "Standard" },
        { Setting_ShowHidden,  "Show hidden items", prefs->show_hidden ? "On" : "Off" },
        { Setting_Rescan,      "Rescan library",    "" },
        { Setting_UnhideAll,   "Unhide everything", "" },
    };

    const int panel_w = 800;
    const int panel_x = (SCREEN_W - panel_w) / 2;
    const int x = panel_x + 40;
    const int right = panel_x + panel_w - 40;
    char line[192];

    /* Dim the library rather than replacing it, so context is kept. */
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H },
                (SDL_Color){ 8, 9, 12, 210 });
    render_fill(render, (SDL_Rect){ panel_x, 28, panel_w, SCREEN_H - 56 }, COL_PANEL);
    render_outline(render, (SDL_Rect){ panel_x, 28, panel_w, SCREEN_H - 56 }, 2, COL_LINE);

    render_text(render, render->font_large, x, 48, COL_TEXT, "Settings");

    int y = 104;
    for (size_t i = 0; i < Setting_Count; i++) {
        /* Section headings sit above the rows they introduce. */
        if (i == Setting_PosterTiles) {
            render_text(render, render->font, x, y, COL_ACCENT, "Appearance");
            y += 30;
        } else if (i == Setting_ShowHidden) {
            y += 12;
            render_text(render, render->font, x, y, COL_ACCENT, "Library");
            y += 30;
        }

        bool active = (i == settings->row);
        if (active)
            render_fill(render, (SDL_Rect){ x - 16, y - 6, panel_w - 48, 36 }, COL_RAISED);

        render_text(render, render->font, x, y, active ? COL_TEXT : COL_DIM,
                    items[i].label);
        if (items[i].value[0])
            render_text_right(render, render->font, right, y,
                              active ? COL_ACCENT : COL_DIM, items[i].value);
        y += 38;
    }

    y += 16;
    render_fill(render, (SDL_Rect){ x - 16, y, panel_w - 48, 1 }, COL_LINE);
    y += 18;

    render_text(render, render->font, x, y, COL_ACCENT, "Controls");
    y += 30;
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        render_text(render, render->font, x, y, COL_TEXT, controls[i][0]);
        render_text(render, render->font, x + 230, y, COL_FAINT, controls[i][1]);
        y += 26;
    }

    y += 14;
    render_fill(render, (SDL_Rect){ x - 16, y, panel_w - 48, 1 }, COL_LINE);
    y += 18;

    render_text(render, render->font, x, y, COL_ACCENT, "About");
    y += 30;

    snprintf(line, sizeof(line), "%zu items across %zu shelves",
             list->count, shelves->count);
    render_text(render, render->font, x, y, COL_FAINT, line);
    y += 26;
    render_text(render, render->font, x, y, COL_FAINT,
                "Config: sdmc:/switch/vitrine/");
    y += 26;

    if (core_note && core_note[0])
        render_text(render, render->font, x, y, COL_WARN, core_note);

    render_text_right(render, render->font, right, SCREEN_H - 62, COL_FAINT,
                      "A  change        -  close");

    SDL_RenderPresent(render->renderer);
}

void ui_draw_mode_gate(Render *render)
{
    static const char *lines[] = {
        "VITRINE needs Application Mode.",
        "",
        "Started this way, hbloader grants only about 56MB of heap -",
        "far too little for emulator cores or high-resolution artwork.",
        "Launching a ROM from here would fail with",
        "\"The software was closed because an error occurred\".",
        "",
        "To fix: return to hbmenu, hold R while launching any game,",
        "then start VITRINE from the menu that appears.",
        "",
        "Press + to exit.",
    };

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, COL_BG);
    render_text(render, render->font_large, MARGIN_X, 120, COL_ACCENT,
                "Wrong launch mode");

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        render_text(render, render->font, MARGIN_X, 190 + (int)i * 32,
                    i == 0 ? COL_TEXT : COL_DIM, lines[i]);

    SDL_RenderPresent(render->renderer);
}

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             ShelfList *shelves, size_t shelf_index, UiState *state,
             const OverrideList *overrides, const char *status)
{
    const Prefs *prefs = &overrides->prefs;
    int pitch = shelf_h(prefs);

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, COL_BG);

    state->pulse = approach(state->pulse * 1000.0f, 1000.0f, 0.25f) / 1000.0f;

    /* Park the active shelf one slot down, so there is context above it. */
    float target_y = (float)shelf_index * pitch;
    if (shelf_index > 0)
        target_y -= pitch * 0.35f;
    state->scroll_y = approach(state->scroll_y, target_y, 0.22f);

    for (size_t i = 0; i < shelves->count; i++) {
        int y = VIEW_TOP + (int)(i * pitch - state->scroll_y);

        if (y + pitch < VIEW_TOP - 40 || y > VIEW_BOTTOM + 40)
            continue;

        draw_shelf(render, icons, list, &shelves->items[i], y, i == shelf_index,
                   state->pulse, overrides, prefs);
    }

    /* Drawn last so shelves scroll under the chrome rather than through it. */
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, HEADER_H }, COL_BG);
    draw_header(render, list, shelves);

    if (prefs->show_hidden)
        render_text_right(render, render->font, SCREEN_W - MARGIN_X, 30, COL_WARN,
                          "showing hidden");

    draw_footer(render, list, shelves, shelf_index, status);

    SDL_RenderPresent(render->renderer);
}
