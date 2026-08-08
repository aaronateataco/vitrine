#pragma once

#include <SDL2/SDL.h>

#include "config.h"
#include "shelves.h"

/*
 * Diagnostics: a log, screenshots, and a one-shot report.
 *
 * The point is to make a hardware session inspectable afterwards. Everything
 * lands under sdmc:/switch/vitrine/ so the whole folder can be copied off the
 * card and read elsewhere.
 */

/// Truncates and opens the log. Safe to call when the path is unwritable.
void diag_open(const char *path);
void diag_close(void);

/*
 * Appends a timestamped line and flushes immediately - a crash mid-session is
 * exactly when the log matters most, so buffering would defeat the purpose.
 */
void diag_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/// Grabs the current framebuffer to dir/vitrine-NNN.png. Returns false on failure.
bool diag_screenshot(SDL_Renderer *renderer, const char *dir, char *out_path,
                     size_t out_path_size);

/*
 * Writes a full state report: firmware, applet type, every configured system
 * with its resolved core, shelf composition, preferences, and the identity of
 * sdmc:/hbmenu.nro - which determines where the console returns after a game
 * exits, and is otherwise guesswork.
 */
bool diag_write_report(const char *path, const EntryList *list,
                       const SystemList *systems, const ShelfList *shelves,
                       const OverrideList *overrides);
