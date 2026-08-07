#pragma once

#include "config.h"

/// Walks each configured system's ROM directory and appends EntryKind_Game entries.
Result roms_scan(EntryList *list, const SystemList *systems);
