#pragma once

#include <SDL2/SDL.h>

#include "entry.h"

typedef struct IconCache IconCache;

IconCache *icons_create(SDL_Renderer *renderer);
void       icons_destroy(IconCache *cache);

/*
 * Decodes on demand and memoises by list index. Returns NULL for entries with no
 * artwork (ROMs) or when decoding fails, so callers must have a fallback.
 */
SDL_Texture *icons_get(IconCache *cache, const Entry *entry, size_t index);
