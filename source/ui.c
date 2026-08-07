#include <stdio.h>
#include <string.h>

#include "ui.h"

#define COLS      80
#define NAME_COLS 44
#define AUTH_COLS 24

#define ROW_HEADER 1
#define ROW_RULE   2
#define ROW_FIRST  3
#define ROW_FOOTER (ROW_FIRST + UI_LIST_ROWS + 1)

#define CLEAR      "\x1b[2J"
#define DIM        "\x1b[37m"
#define RESET      "\x1b[0m"
#define HIGHLIGHT  "\x1b[30;47m"

static void move_to(int row)
{
    printf("\x1b[%d;1H", row);
}

static void draw_rule(int row)
{
    move_to(row);
    printf(DIM);
    for (int i = 0; i < COLS; i++)
        putchar('-');
    printf(RESET);
}

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
    *homebrew = 0;
    *games = 0;
    *titles = 0;

    for (size_t i = 0; i < list->count; i++) {
        switch (list->items[i].kind) {
            case EntryKind_Homebrew: (*homebrew)++; break;
            case EntryKind_Game:     (*games)++;    break;
            default:                 (*titles)++;   break;
        }
    }
}

void ui_draw(const EntryList *list, const size_t *view, size_t view_count,
             size_t selected, size_t scroll, Filter filter, const char *status)
{
    size_t homebrew = 0;
    size_t games = 0;
    size_t titles = 0;
    count_kinds(list, &homebrew, &games, &titles);

    printf(CLEAR);

    move_to(ROW_HEADER);
    printf(" LUDI-NX");
    printf("\x1b[%d;%dH", ROW_HEADER, COLS - 45);
    printf(DIM "%3zu hb   %4zu roms   %3zu switch   [%s]" RESET, homebrew, games,
           titles, ui_filter_name(filter));

    draw_rule(ROW_RULE);

    for (size_t row = 0; row < UI_LIST_ROWS; row++) {
        size_t index = scroll + row;
        move_to(ROW_FIRST + (int)row);

        if (index >= view_count)
            continue;

        const Entry *entry = &list->items[view[index]];
        bool active = (index == selected);

        printf("%s %c %s  %-*.*s  " DIM "%-*.*s" RESET,
               active ? HIGHLIGHT : "",
               active ? '>' : ' ',
               entry->kind == EntryKind_Homebrew ? "HB"
                   : entry->kind == EntryKind_Game ? "GM" : "SW",
               NAME_COLS, NAME_COLS, entry->name,
               AUTH_COLS, AUTH_COLS, entry->author);

        if (active)
            printf(RESET);
    }

    draw_rule(ROW_FOOTER);

    move_to(ROW_FOOTER + 1);
    if (view_count == 0)
        printf(" nothing to show");
    else
        printf(" %zu/%zu", selected + 1, view_count);

    printf("\x1b[%d;%dH", ROW_FOOTER + 1, COLS - 40);
    printf(DIM "A launch   X filter   Y rescan   + exit" RESET);

    if (status && status[0]) {
        move_to(ROW_FOOTER + 2);
        printf(" %.*s", COLS - 2, status);
    }
}
