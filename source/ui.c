#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

/*
 * Layout. Three shelves fit the viewport with the fourth partly visible, which
 * is what tells the eye the page continues without needing a scrollbar.
 */
#define MARGIN_X     64
#define HEADER_H     76
#define FOOTER_H    108
#define TILE        132
#define TILE_GAP     18
#define SHELF_LABEL  30
#define SHELF_PAD    26
#define SHELF_H     (SHELF_LABEL + TILE + SHELF_PAD)
#define VIEW_TOP     HEADER_H
#define VIEW_BOTTOM (SCREEN_H - FOOTER_H)
#define SELECT_SCALE 1.10f

static const SDL_Color COL_BG       = {  14,  16,  20, 255 };
static const SDL_Color COL_PANEL    = {  26,  30,  38, 255 };
static const SDL_Color COL_TEXT     = { 240, 242, 246, 255 };
static const SDL_Color COL_DIM      = { 138, 144, 160, 255 };
static const SDL_Color COL_FAINT    = {  74,  80,  96, 255 };
static const SDL_Color COL_ACCENT   = {  90, 169, 255, 255 };
static const SDL_Color COL_WARN     = { 236, 130, 130, 255 };

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

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
    SDL_Color out;
    out.r = (Uint8)(a.r + (b.r - a.r) * t);
    out.g = (Uint8)(a.g + (b.g - a.g) * t);
    out.b = (Uint8)(a.b + (b.b - a.b) * t);
    out.a = (Uint8)(a.a + (b.a - a.a) * t);
    return out;
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
                      bool hidden)
{
    int size = TILE;
    int ox = x;
    int oy = y;

    if (selected) {
        /* Grow from the centre so neighbours are not pushed visually. */
        float scale = 1.0f + (SELECT_SCALE - 1.0f) * pulse;
        size = (int)(TILE * scale);
        ox = x - (size - TILE) / 2;
        oy = y - (size - TILE) / 2;
    }

    SDL_Rect rect = { ox, oy, size, size };

    if (selected)
        render_shadow(render, rect, 10);

    SDL_Texture *texture = icons_get(icons, entry, entry_index);

    if (texture) {
        SDL_RenderCopy(render->renderer, texture, NULL, &rect);
    } else {
        const char *label = entry->author[0] ? entry->author : entry->name;
        render_fill(render, rect, plate_color(label));
        render_text_fit(render, render->font, rect.x, rect.y + size / 2 - 14,
                        size, COL_TEXT, label);
    }

    if (hidden) {
        /* Only visible in "show hidden" mode, so it must read as excluded. */
        SDL_Color veil = { 14, 16, 20, 170 };
        render_fill(render, rect, veil);
        render_text_fit(render, render->font, rect.x, rect.y + 6, size,
                        COL_WARN, "hidden");
    }

    if (selected) {
        render_outline(render, rect, 3, COL_ACCENT);
    } else if (!hidden) {
        /* Recede unselected tiles rather than dimming the whole shelf. */
        SDL_Color veil = { 14, 16, 20, 110 };
        render_fill(render, rect, veil);
    }
}

static void draw_shelf(Render *render, IconCache *icons, const EntryList *list,
                       Shelf *shelf, int y, bool active, float pulse,
                       const OverrideList *overrides)
{
    char meta[64];

    SDL_Color label_color = active ? COL_TEXT : COL_DIM;
    render_text(render, render->font, MARGIN_X, y, label_color, shelf->name);

    snprintf(meta, sizeof(meta), "%zu", shelf->count);
    render_text(render, render->font, MARGIN_X + 460, y + 3, COL_FAINT, meta);

    if (active) {
        SDL_Rect bar = { MARGIN_X - 14, y + 2, 4, 20 };
        render_fill(render, bar, COL_ACCENT);
    }

    int strip_y = y + SHELF_LABEL;
    int visible_w = SCREEN_W - 2 * MARGIN_X;

    /* Keep the cursor centred, but never scroll past either end. */
    float total_w = (float)shelf->count * (TILE + TILE_GAP) - TILE_GAP;
    float target = (float)shelf->cursor * (TILE + TILE_GAP) + TILE / 2.0f
                 - visible_w / 2.0f;
    float max_scroll = total_w > visible_w ? total_w - visible_w : 0.0f;

    if (target < 0.0f)        target = 0.0f;
    if (target > max_scroll)  target = max_scroll;

    shelf->scroll_x = approach(shelf->scroll_x, target, 0.22f);

    SDL_Rect clip = { MARGIN_X - 12, strip_y - 12, visible_w + 24, TILE + 24 };
    SDL_RenderSetClipRect(render->renderer, &clip);

    for (size_t i = 0; i < shelf->count; i++) {
        int x = MARGIN_X + (int)(i * (TILE + TILE_GAP) - shelf->scroll_x);
        if (x + TILE < MARGIN_X - 24 || x > SCREEN_W)
            continue;   /* Off-screen: skip the icon decode entirely. */

        const Entry *entry = &list->items[shelf->items[i]];
        bool hidden = overrides && overrides_hidden(overrides, entry);
        draw_tile(render, icons, entry, shelf->items[i], x, strip_y,
                  active && i == shelf->cursor, pulse, hidden);
    }

    SDL_RenderSetClipRect(render->renderer, NULL);

    /* Fade the strip into the margins so clipped tiles do not end abruptly. */
    SDL_Color edge = COL_BG;
    edge.a = 255;
    render_edge_fade(render, (SDL_Rect){ 0, strip_y - 12, MARGIN_X - 12, TILE + 24 },
                     edge, false);
    render_edge_fade(render, (SDL_Rect){ SCREEN_W - MARGIN_X + 12, strip_y - 12,
                                         MARGIN_X - 12, TILE + 24 }, edge, true);
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
        render_text(render, render->font_large, MARGIN_X, y, COL_DIM,
                    "Nothing found");
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

    render_text(render, render->font, SCREEN_W - 560, y + 42, COL_FAINT,
                "A play   X hide   ZL retag   ZR show hidden   Y rescan   + exit");

    if (status && status[0])
        render_text(render, render->font, MARGIN_X, y + 70, COL_WARN, status);
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
             const OverrideList *overrides, bool show_hidden, const char *status)
{
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, COL_BG);

    state->pulse = approach(state->pulse * 1000.0f, 1000.0f, 0.25f) / 1000.0f;

    /* Park the active shelf one slot down, so there is context above it. */
    float target_y = (float)shelf_index * SHELF_H;
    if (shelf_index > 0)
        target_y -= SHELF_H * 0.35f;
    state->scroll_y = approach(state->scroll_y, target_y, 0.22f);

    for (size_t i = 0; i < shelves->count; i++) {
        int y = VIEW_TOP + (int)(i * SHELF_H - state->scroll_y);

        if (y + SHELF_H < VIEW_TOP - 40 || y > VIEW_BOTTOM + 40)
            continue;

        draw_shelf(render, icons, list, &shelves->items[i], y,
                   i == shelf_index, state->pulse, overrides);
    }

    /* Drawn last so shelves scroll under the chrome rather than through it. */
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, HEADER_H }, COL_BG);
    draw_header(render, list, shelves);

    if (show_hidden)
        render_text(render, render->font, SCREEN_W - 200, 30, COL_WARN,
                    "showing hidden");

    draw_footer(render, list, shelves, shelf_index, status);

    SDL_RenderPresent(render->renderer);
}
