#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

#define MARGIN_X     64
#define HEADER_H     76
#define FOOTER_H    124
#define TILE_GAP     18
#define SHELF_LABEL  40
#define SHELF_PAD    22
#define VIEW_TOP     HEADER_H
#define VIEW_BOTTOM (SCREEN_H - FOOTER_H)
#define SELECT_SCALE 1.10f

/*
 * Themes. The Switch 2 pair follows what the console actually ships: only
 * Basic Dark and Basic Light, monochrome with no accent hue. Colour values
 * only - no Nintendo assets are involved.
 */
typedef struct {
    const char *name;
    SDL_Color bg, panel, raised, line, text, dim, faint, accent, warn;
} Theme;

static const Theme THEMES[] = {
    { "Vitrine Dark",
      {  14,  16,  20, 255 }, {  26,  30,  38, 255 }, {  38,  46,  62, 255 },
      {  52,  58,  74, 255 }, { 240, 242, 246, 255 }, { 138, 144, 160, 255 },
      {  74,  80,  96, 255 }, {  90, 169, 255, 255 }, { 236, 130, 130, 255 } },

    /* Switch 2 ships only Basic Dark and Basic Light - monochrome, no accent
       hue - so these deliberately use white/black rather than Nintendo red. */
    { "Switch 2 Dark",
      {  15,  15,  15, 255 }, {  38,  38,  38, 255 }, {  64,  64,  64, 255 },
      {  82,  82,  82, 255 }, { 255, 255, 255, 255 }, { 168, 168, 168, 255 },
      { 112, 112, 112, 255 }, { 255, 255, 255, 255 }, { 240, 140, 140, 255 } },

    { "Switch 2 Light",
      { 235, 235, 235, 255 }, { 255, 255, 255, 255 }, { 214, 214, 214, 255 },
      { 190, 190, 190, 255 }, {  26,  26,  26, 255 }, {  96,  96,  96, 255 },
      { 140, 140, 140, 255 }, {  26,  26,  26, 255 }, { 176,  46,  46, 255 } },

    { "Daylight",
      { 242, 243, 246, 255 }, { 255, 255, 255, 255 }, { 224, 228, 236, 255 },
      { 204, 209, 219, 255 }, {  24,  26,  32, 255 }, {  92,  98, 112, 255 },
      { 142, 148, 162, 255 }, {   0, 112, 224, 255 }, { 186,  40,  40, 255 } },
};

#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static const Theme *g_theme = &THEMES[0];

int ui_theme_count(void) { return THEME_COUNT; }

const char *ui_theme_name(int index)
{
    if (index < 0 || index >= THEME_COUNT)
        index = 0;
    return THEMES[index].name;
}

static void theme_apply(const Prefs *prefs)
{
    int index = prefs->theme;
    if (index < 0 || index >= THEME_COUNT)
        index = 0;
    g_theme = &THEMES[index];
}

size_t ui_settings_count(const ShelfList *shelves)
{
    return Setting_Fixed + shelves->count + shelves->hidden_count;
}

/*
 * Tile geometry follows the display preferences. Posters use the 2:3 ratio that
 * SteamGridDB grids ship in; square matches the Switch's own 1:1 icons.
 */
static int tile_w(const Prefs *prefs)
{
    static const int square[] = { 132, 176, 224 };
    static const int poster[] = { 116, 152, 196 };

    int step = prefs->cover_size;
    if (step < 0 || step > 2)
        step = 0;

    return prefs->poster_tiles ? poster[step] : square[step];
}

static int tile_h(const Prefs *prefs)
{
    int w = tile_w(prefs);
    return prefs->poster_tiles ? w * 3 / 2 : w;
}

static const char *cover_size_name(const Prefs *prefs)
{
    switch (prefs->cover_size) {
        case 1:  return "Large";
        case 2:  return "Extra large";
        default: return "Standard";
    }
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

    SDL_Texture *texture = icons_get(icons, entry, entry_index, prefs->poster_tiles);

    if (texture) {
        SDL_RenderCopy(render->renderer, texture, NULL, &rect);
    } else {
        /* Tint by platform, but label with the title - the platform is already
           the shelf heading, and repeating it makes every tile look the same. */
        const char *tint = entry->author[0] ? entry->author : entry->name;
        render_fill(render, rect, plate_color(tint));
        render_text_fit(render, render->font, rect.x, rect.y + h / 2 - 14, w,
                        g_theme->text, entry->name);
    }

    if (hidden) {
        /* Only visible in "show hidden" mode, so it must read as excluded. */
        SDL_Color veil = g_theme->bg;
        veil.a = 170;
        render_fill(render, rect, veil);
        render_text_fit(render, render->font, rect.x, rect.y + 6, w, g_theme->warn,
                        "hidden");
    }

    /* Switch 2 rounds its icons; scale the radius with the tile. */
    render_round_corners(render, rect, w / 12, g_theme->bg);

    if (selected)
        render_outline(render, rect, 3, g_theme->accent);
    else if (!hidden) {
        /* Recede unselected tiles rather than dimming the whole shelf. */
        SDL_Color veil = g_theme->bg;
        veil.a = 110;
        render_fill(render, rect, veil);
    }
}

static void draw_shelf(Render *render, IconCache *icons, const EntryList *list,
                       Shelf *shelf, int y, bool active, float pulse,
                       const OverrideList *overrides, const Prefs *prefs)
{
    char meta[64];
    int w = tile_w(prefs);
    int h = tile_h(prefs);
    int pitch = w + TILE_GAP;

    render_text(render, render->font, MARGIN_X, y, active ? g_theme->text : g_theme->dim,
                shelf->name);

    snprintf(meta, sizeof(meta), "%zu", shelf->count);
    render_text_right(render, render->font, SCREEN_W - MARGIN_X, y + 3, g_theme->faint, meta);

    if (active)
        render_fill(render, (SDL_Rect){ MARGIN_X - 14, y + 2, 4, 20 }, g_theme->accent);

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
    SDL_Color edge = g_theme->bg;
    render_edge_fade(render, (SDL_Rect){ 0, strip_y - 12, MARGIN_X - 12, h + 24 },
                     edge, false);
    render_edge_fade(render, (SDL_Rect){ SCREEN_W - MARGIN_X + 12, strip_y - 12,
                                         MARGIN_X - 12, h + 24 }, edge, true);
}

static void draw_header(Render *render, const EntryList *list, const ShelfList *shelves)
{
    char line[128];

    render_text(render, render->font_large, MARGIN_X, 22, g_theme->text, "VITRINE");

    snprintf(line, sizeof(line), "%zu items across %zu shelves",
             list->count, shelves->count);
    render_text(render, render->font, MARGIN_X + 190, 30, g_theme->faint, line);

    render_fill(render, (SDL_Rect){ 0, HEADER_H - 2, SCREEN_W, 1 }, g_theme->panel);
}

static void draw_footer(Render *render, const EntryList *list,
                        const ShelfList *shelves, size_t shelf_index,
                        const char *status)
{
    char line[256];
    int y = VIEW_BOTTOM + 16;

    render_fill(render, (SDL_Rect){ 0, VIEW_BOTTOM, SCREEN_W, FOOTER_H }, g_theme->panel);

    if (shelves->count == 0) {
        render_text(render, render->font_large, MARGIN_X, y, g_theme->dim, "Nothing found");
        render_text(render, render->font, MARGIN_X, y + 40, g_theme->faint,
                    "Check systems.ini, then press Y to rescan");
        return;
    }

    const Shelf *shelf = &shelves->items[shelf_index];
    const Entry *entry = &list->items[shelf->items[shelf->cursor]];

    render_text(render, render->font_large, MARGIN_X, y, g_theme->text, entry->name);

    const char *kind = entry->kind == EntryKind_Homebrew ? "Homebrew"
                     : entry->kind == EntryKind_Game     ? "ROM"
                                                         : "Installed";
    if (entry->author[0])
        snprintf(line, sizeof(line), "%s   %s   %zu of %zu",
                 entry->author, kind, shelf->cursor + 1, shelf->count);
    else
        snprintf(line, sizeof(line), "%s   %zu of %zu",
                 kind, shelf->cursor + 1, shelf->count);

    render_text(render, render->font, MARGIN_X, y + 42, g_theme->dim, line);

    /* Right-aligned from a measured width, so it can never run off screen. */
    render_text_right(render, render->font, SCREEN_W - MARGIN_X, y + 42, g_theme->faint,
                      "A  play        -  settings");

    if (status && status[0])
        render_text(render, render->font, MARGIN_X, y + 74, g_theme->warn, status);
}

void ui_draw_cover_picker(Render *render, const CoverPicker *picker,
                          const Entry *entry, const Prefs *prefs)
{
    enum { VISIBLE = 9, ROW_H = 36 };

    theme_apply(prefs);

    const int list_x = 72;
    const int list_w = 520;
    const int preview_x = 640;
    const int preview_w = 560;
    char line[192];

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, g_theme->bg);
    render_text(render, render->font_large, list_x, 40, g_theme->text, "Choose a cover");
    render_text_fit(render, render->font, list_x, 82, list_w, g_theme->dim, entry->name);

    if (picker->covers.count == 0) {
        render_text(render, render->font, list_x, 150, g_theme->warn,
                    picker->message[0] ? picker->message : "No covers found");
        render_text_right(render, render->font, SCREEN_W - 72, SCREEN_H - 56,
                          g_theme->faint, "B  back");
        SDL_RenderPresent(render->renderer);
        return;
    }

    size_t start = 0;
    if (picker->covers.count > VISIBLE) {
        if (picker->index > VISIBLE / 2)
            start = picker->index - VISIBLE / 2;
        if (start + VISIBLE > picker->covers.count)
            start = picker->covers.count - VISIBLE;
    }

    int y = 132;
    for (size_t i = start; i < picker->covers.count && i < start + VISIBLE; i++) {
        const SgdbCover *cover = &picker->covers.items[i];
        bool active = (i == picker->index);

        if (active)
            render_fill(render, (SDL_Rect){ list_x - 14, y - 6, list_w + 28, ROW_H - 2 },
                        g_theme->raised);

        snprintf(line, sizeof(line), "%s%s", cover->preferred ? "* " : "  ",
                 cover->author[0] ? cover->author : "unknown uploader");
        render_text(render, render->font, list_x, y, active ? g_theme->text : g_theme->dim,
                    line);

        snprintf(line, sizeof(line), "%d", cover->score);
        render_text_right(render, render->font, list_x + list_w, y, g_theme->faint, line);
        y += ROW_H;
    }

    /* Preview pane, letterboxed into the available area. */
    SDL_Rect frame = { preview_x, 132, preview_w, 420 };
    render_fill(render, frame, g_theme->panel);

    if (picker->preview && picker->preview_index == picker->index) {
        int tw = 0;
        int th = 0;
        SDL_QueryTexture(picker->preview, NULL, NULL, &tw, &th);

        if (tw > 0 && th > 0) {
            float scale = (float)frame.w / tw;
            float fit_h = (float)frame.h / th;
            if (fit_h < scale)
                scale = fit_h;

            SDL_Rect dst = {
                frame.x + (frame.w - (int)(tw * scale)) / 2,
                frame.y + (frame.h - (int)(th * scale)) / 2,
                (int)(tw * scale), (int)(th * scale)
            };
            SDL_RenderCopy(render->renderer, picker->preview, NULL, &dst);
            render_round_corners(render, dst, dst.w / 12, g_theme->panel);
        }
    } else {
        render_text_fit(render, render->font, frame.x, frame.y + frame.h / 2 - 12,
                        frame.w, g_theme->faint, "loading preview...");
    }

    snprintf(line, sizeof(line), "%zu of %zu   *  preferred uploader",
             picker->index + 1, picker->covers.count);
    render_text(render, render->font, list_x, SCREEN_H - 92, g_theme->faint, line);

    if (picker->message[0])
        render_text(render, render->font, list_x, SCREEN_H - 60, g_theme->warn,
                    picker->message);

    render_text_right(render, render->font, SCREEN_W - 72, SCREEN_H - 60,
                      g_theme->faint, "A  use this cover        B  back");

    SDL_RenderPresent(render->renderer);
}

void ui_draw_settings(Render *render, const Settings *settings, const Prefs *prefs,
                      const EntryList *list, const ShelfList *shelves,
                      const OverrideList *overrides, const char *core_note)
{
    enum { VISIBLE_ROWS = 11, ROW_H = 34 };

    theme_apply(prefs);

    const int panel_w = 820;
    const int panel_x = (SCREEN_W - panel_w) / 2;
    const int x = panel_x + 40;
    const int right = panel_x + panel_w - 40;

    size_t total = ui_settings_count(shelves);
    char line[192];

    /* Dim the library rather than replacing it, so context is kept. */
    SDL_Color scrim = g_theme->bg;
    scrim.a = 214;
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, scrim);
    render_fill(render, (SDL_Rect){ panel_x, 28, panel_w, SCREEN_H - 56 }, g_theme->panel);
    render_outline(render, (SDL_Rect){ panel_x, 28, panel_w, SCREEN_H - 56 }, 2,
                   g_theme->line);

    render_text(render, render->font_large, x, 48, g_theme->text, "Settings");
    snprintf(line, sizeof(line), "%s", ui_theme_name(prefs->theme));
    render_text_right(render, render->font, right, 56, g_theme->faint, line);

    /* Scroll so the selection stays inside the window. */
    size_t start = 0;
    if (total > VISIBLE_ROWS) {
        if (settings->row > VISIBLE_ROWS / 2)
            start = settings->row - VISIBLE_ROWS / 2;
        if (start + VISIBLE_ROWS > total)
            start = total - VISIBLE_ROWS;
    }

    int y = 108;
    for (size_t i = start; i < total && i < start + VISIBLE_ROWS; i++) {
        const char *label = "";
        const char *value = "";
        char shelf_value[32];

        if (i == Setting_Theme && i == start) {
            render_text(render, render->font, x, y, g_theme->accent, "Appearance");
            y += 28;
        } else if (i == Setting_SgdbKey) {
            render_text(render, render->font, x, y, g_theme->accent, "Covers");
            y += 28;
        } else if (i == Setting_RaUser) {
            render_text(render, render->font, x, y, g_theme->accent, "Achievements");
            y += 28;
        } else if (i == Setting_ShowHidden) {
            render_text(render, render->font, x, y, g_theme->accent, "Library");
            y += 28;
        } else if (i == Setting_Fixed) {
            render_text(render, render->font, x, y, g_theme->accent, "Shelves");
            y += 28;
        }

        switch (i) {
            case Setting_Theme:
                label = "Theme";
                value = ui_theme_name(prefs->theme);
                break;
            case Setting_PosterTiles:
                label = "Cover shape";
                value = prefs->poster_tiles ? "Poster 2:3" : "Square 1:1";
                break;
            case Setting_LargeTiles:
                label = "Cover size";
                value = cover_size_name(prefs);
                break;
            case Setting_ShowHidden:
                label = "Show hidden items";
                value = prefs->show_hidden ? "On" : "Off";
                break;
            case Setting_SgdbKey:
                label = "SteamGridDB API key";
                value = prefs->sgdb_key[0] ? "Set" : "Not set";
                break;
            case Setting_FetchCovers:
                label = "Download covers for this shelf";
                break;
            case Setting_RaUser:
                label = "RetroAchievements user";
                value = prefs->ra_user[0] ? prefs->ra_user : "Not set";
                break;
            case Setting_RaKey:
                label = "RetroAchievements key";
                value = prefs->ra_key[0] ? "Set" : "Not set";
                break;
            case Setting_TrophyRoom:
                label = "Open Trophy Room";
                break;
            case Setting_Rescan:
                label = "Rescan library";
                break;
            case Setting_UnhideAll:
                label = "Unhide everything";
                break;
            case Setting_Diagnostics:
                label = "Save screenshot + report";
                break;
            default: {
                size_t index = i - Setting_Fixed;

                /* Visible shelves first, then the ones hidden out of the list. */
                label = index < shelves->count
                            ? shelves->items[index].name
                            : shelves->hidden_names[index - shelves->count];

                snprintf(shelf_value, sizeof(shelf_value), "%s",
                         overrides && overrides_shelf_hidden(overrides, label)
                             ? "Hidden" : "Shown");
                value = shelf_value;
                break;
            }
        }

        bool active = (i == settings->row);
        if (active)
            render_fill(render, (SDL_Rect){ x - 16, y - 5, panel_w - 48, ROW_H - 2 },
                        g_theme->raised);

        render_text(render, render->font, x, y, active ? g_theme->text : g_theme->dim,
                    label);
        if (value[0])
            render_text_right(render, render->font, right, y,
                              active ? g_theme->accent : g_theme->dim, value);
        y += ROW_H;
    }

    if (total > VISIBLE_ROWS) {
        snprintf(line, sizeof(line), "%zu of %zu", settings->row + 1, total);
        render_text_right(render, render->font, right, y + 6, g_theme->faint, line);
    }

    /* Footer block: counts, paths, and anything actively wrong. */
    int info_y = SCREEN_H - 168;
    render_fill(render, (SDL_Rect){ x - 16, info_y - 18, panel_w - 48, 1 },
                g_theme->line);

    snprintf(line, sizeof(line), "%zu items across %zu shelves",
             list->count, shelves->count);
    render_text(render, render->font, x, info_y, g_theme->faint, line);
    render_text(render, render->font, x, info_y + 26, g_theme->faint,
                "Config: sdmc:/switch/vitrine/");

    if (core_note && core_note[0])
        render_text(render, render->font, x, info_y + 52, g_theme->warn, core_note);

    render_text_right(render, render->font, right, SCREEN_H - 62, g_theme->faint,
                      "A  change        -  close");

    SDL_RenderPresent(render->renderer);
}

SDL_Rect ui_room_begin(Render *render, const Prefs *prefs)
{
    theme_apply(prefs);

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, g_theme->bg);

    /* Leaves room for a title above and attribution below. */
    SDL_Rect viewport = { 0, 96, SCREEN_W, SCREEN_H - 200 };
    return viewport;
}

void ui_room_end(Render *render, const Prefs *prefs, const char *title,
                 const char *subtitle, const char *source)
{
    theme_apply(prefs);

    render_text(render, render->font_large, MARGIN_X, 34, g_theme->text, title);

    if (subtitle && subtitle[0])
        render_text_right(render, render->font, SCREEN_W - MARGIN_X, 46,
                          g_theme->faint, subtitle);

    /* Attribution for a model the user supplied, when the room names a source. */
    if (source && source[0])
        render_text(render, render->font, MARGIN_X, SCREEN_H - 96, g_theme->faint,
                    source);

    render_text(render, render->font, MARGIN_X, SCREEN_H - 60, g_theme->faint,
                "Right stick  orbit        L / R  zoom        B  back");

    SDL_RenderPresent(render->renderer);
}

void ui_draw_mode_gate(Render *render)
{
    g_theme = &THEMES[0];

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

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, g_theme->bg);
    render_text(render, render->font_large, MARGIN_X, 120, g_theme->accent,
                "Wrong launch mode");

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        render_text(render, render->font, MARGIN_X, 190 + (int)i * 32,
                    i == 0 ? g_theme->text : g_theme->dim, lines[i]);

    SDL_RenderPresent(render->renderer);
}

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             ShelfList *shelves, size_t shelf_index, UiState *state,
             const OverrideList *overrides, const char *status)
{
    const Prefs *prefs = &overrides->prefs;
    theme_apply(prefs);

    int pitch = shelf_h(prefs);

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, g_theme->bg);

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
    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, HEADER_H }, g_theme->bg);
    draw_header(render, list, shelves);

    if (prefs->show_hidden)
        render_text_right(render, render->font, SCREEN_W - MARGIN_X, 30, g_theme->warn,
                          "showing hidden");

    draw_footer(render, list, shelves, shelf_index, status);

    SDL_RenderPresent(render->renderer);
}
