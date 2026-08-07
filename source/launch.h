#pragma once

#include "config.h"

bool   launch_can_launch_homebrew(void);
bool   launch_can_launch_title(void);

/// `systems` is only consulted for EntryKind_Game and may be NULL otherwise.
Result launch_entry(const Entry *entry, const SystemList *systems);
