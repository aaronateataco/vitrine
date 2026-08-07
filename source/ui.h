#pragma once

#include "entry.h"
#include "icons.h"
#include "render.h"

typedef enum {
    Filter_All      = 0,
    Filter_Homebrew = 1,
    Filter_Games    = 2,
    Filter_Titles   = 3,
    Filter_Count    = 4,
} Filter;

enum {
    UI_COLS = 6,
    UI_ROWS = 3,
    UI_PAGE = UI_COLS * UI_ROWS,
};

const char *ui_filter_name(Filter filter);

/// `scroll_row` is the topmost visible grid row, not an entry index.
void ui_draw(Render *render, IconCache *icons, const EntryList *list,
             const size_t *view, size_t view_count, size_t selected,
             size_t scroll_row, Filter filter, const char *status);
