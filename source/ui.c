#include <stdio.h>
#include <string.h>

#include "ui.h"

/* Grid metrics, sized so three rows and a footer fit 720p without crowding. */
#define GRID_X0   135
#define GRID_Y0    84
#define ICON       150
#define GAP_X       22
#define GAP_Y       18
#define LABEL_H     24
#define CELL_H     (ICON + LABEL_H)
#define FOOTER_Y   650

static const SDL_Color COL_BG      = {  20,  22,  28, 255 };
static const SDL_Color COL_TILE    = {  35,  39,  51, 255 };
static const SDL_Color COL_TEXT    = { 232, 234, 240, 255 };
static const SDL_Color COL_DIM     = { 139, 145, 163, 255 };
static const SDL_Color COL_ACCENT  = {  74, 163, 255, 255 };
static const SDL_Color COL_SHADE   = {  10,  11,  15, 255 };

const char *ui_filter_name(Filter filter)
{
    switch (filter) {
        case Filter_Homebrew: return "homebrew";
        case Filter_Games:    return "roms";
        case Filter_Titles:   return "switch";
        default:              return "all";
    }
}

static void count_kinds(const EntryList *list, size_t *homebrew, size_t *games,
                        size_t *titles)
{
    *homebrew = *games = *titles = 0;

    for (size_t i = 0; i < list->count; i++) {
        switch (list->items[i].kind) {
            case EntryKind_Homebrew: (*homebrew)++; break;
            case EntryKind_Game:     (*games)++;    break;
            default:                 (*titles)++;   break;
        }
    }
}

/* ROMs have no artwork, so derive a stable tile colour from the system name. */
static SDL_Color placeholder_color(const char *seed)
{
    unsigned hash = 2166136261u;
    for (const char *p = seed; *p; p++)
        hash = (hash ^ (unsigned char)*p) * 16777619u;

    SDL_Color color;
    color.r = (Uint8)(60 + (hash        & 0x3f));
    color.g = (Uint8)(60 + ((hash >> 8)  & 0x3f));
    color.b = (Uint8)(80 + ((hash >> 16) & 0x5f));
    color.a = 255;
    return color;
}

static void draw_tile(Render *render, IconCache *icons, const Entry *entry,
                      size_t index, SDL_Rect cell, bool selected)
{
    SDL_Rect icon_rect = { cell.x, cell.y, ICON, ICON };

    if (selected) {
        SDL_Rect glow = { cell.x - 8, cell.y - 8, ICON + 16, ICON + 16 };
        render_fill(render, glow, COL_ACCENT);
    }

    SDL_Texture *texture = icons_get(icons, entry, index);

    if (texture) {
        SDL_RenderCopy(render->renderer, texture, NULL, &icon_rect);
    } else {
        /* No artwork: a coloured plate carrying the system or kind name. */
        const char *label = entry->author[0] ? entry->author : "homebrew";
        render_fill(render, icon_rect, placeholder_color(label));
        render_text_fit(render, render->font, icon_rect.x,
                        icon_rect.y + ICON / 2 - 12, ICON, COL_TEXT, label);
    }

    if (!selected) {
        /* Push unselected tiles back so the cursor reads clearly. */
        SDL_Color shade = COL_SHADE;
        shade.a = 90;
        render_fill(render, icon_rect, shade);
    }

    render_text_fit(render, render->font, cell.x, cell.y + ICON + 2, ICON,
                    selected ? COL_TEXT : COL_DIM, entry->name);
}

void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             const size_t *view, size_t view_count, size_t selected,
             size_t scroll_row, Filter filter, const char *status)
{
    char line[256];

    render_fill(render, (SDL_Rect){ 0, 0, SCREEN_W, SCREEN_H }, COL_BG);

    /* Header */
    render_text(render, render->font_large, GRID_X0, 24, COL_TEXT, "LUDI-NX");

    size_t homebrew = 0, games = 0, titles = 0;
    count_kinds(list, &homebrew, &games, &titles);
    snprintf(line, sizeof(line), "%zu homebrew   %zu roms   %zu switch   [%s]",
             homebrew, games, titles, ui_filter_name(filter));
    render_text(render, render->font, GRID_X0 + 170, 34, COL_DIM, line);

    /* Grid */
    for (size_t row = 0; row < UI_ROWS; row++) {
        for (size_t col = 0; col < UI_COLS; col++) {
            size_t slot = (scroll_row + row) * UI_COLS + col;
            if (slot >= view_count)
                continue;

            SDL_Rect cell = {
                GRID_X0 + (int)col * (ICON + GAP_X),
                GRID_Y0 + (int)row * (CELL_H + GAP_Y),
                ICON, CELL_H
            };

            const Entry *entry = &list->items[view[slot]];
            draw_tile(render, icons, entry, view[slot], cell, slot == selected);
        }
    }

    /* Footer: full detail for the current selection. */
    render_fill(render, (SDL_Rect){ 0, FOOTER_Y - 12, SCREEN_W, 2 },
                (SDL_Color){ 45, 49, 62, 255 });

    if (view_count > 0 && selected < view_count) {
        const Entry *entry = &list->items[view[selected]];
        render_text(render, render->font_large, GRID_X0, FOOTER_Y, COL_TEXT, entry->name);

        const char *kind = entry->kind == EntryKind_Homebrew ? "homebrew"
                         : entry->kind == EntryKind_Game     ? "rom"
                                                             : "switch game";
        snprintf(line, sizeof(line), "%s%s%s   -   %zu/%zu",
                 entry->author, entry->author[0] ? "   -   " : "", kind,
                 selected + 1, view_count);
        render_text(render, render->font, GRID_X0, FOOTER_Y + 38, COL_DIM, line);
    } else {
        render_text(render, render->font_large, GRID_X0, FOOTER_Y, COL_DIM,
                    "nothing to show");
    }

    render_text(render, render->font, SCREEN_W - 430, FOOTER_Y + 38, COL_DIM,
                "A launch    X filter    Y rescan    + exit");

    if (status && status[0])
        render_text(render, render->font, GRID_X0, FOOTER_Y + 62,
                    (SDL_Color){ 235, 130, 130, 255 }, status);

    SDL_RenderPresent(render->renderer);
}
