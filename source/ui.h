#pragma once

#include "entry.h"

typedef enum {
    Filter_All      = 0,
    Filter_Homebrew = 1,
    Filter_Games    = 2,
    Filter_Titles   = 3,
    Filter_Count    = 4,
} Filter;

enum {
    UI_LIST_ROWS = 34,
};

const char *ui_filter_name(Filter filter);

void ui_draw(const EntryList *list, const size_t *view, size_t view_count,
             size_t selected, size_t scroll, Filter filter, const char *status);
