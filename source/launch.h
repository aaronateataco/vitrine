#pragma once

#include "config.h"

bool   launch_can_launch_homebrew(void);
bool   launch_can_launch_title(void);

/*
 * True only under title takeover. hbloader gives applet mode roughly 56MB of
 * heap against gigabytes in Application Mode, which is far too little for
 * emulator cores or high-resolution artwork.
 */
bool   launch_is_application_mode(void);

/// `systems` is only consulted for EntryKind_Game and may be NULL otherwise.
Result launch_entry(const Entry *entry, const SystemList *systems);
