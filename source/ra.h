#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * RetroAchievements client for the Trophy Room.
 *
 * Uses the public web API with the user's own username and web API key. Badge
 * images are cached on the SD card; nothing is bundled with the app.
 *
 * Free of SDL and GL headers so it stays host-testable.
 */

enum {
    RA_MAX_TROPHIES = 32,
    RA_TITLE_LEN    = 96,
    RA_BADGE_LEN    = 32,
    RA_URL_LEN      = 256,
};

typedef struct {
    char title[RA_TITLE_LEN];
    char game[RA_TITLE_LEN];
    char badge[RA_BADGE_LEN];   ///< Badge id, not a URL.
    int  points;
} RaTrophy;

typedef struct {
    RaTrophy items[RA_MAX_TROPHIES];
    size_t   count;
} RaTrophyList;

/// Parses the API's JSON array. Exposed separately so it can be tested offline.
bool ra_parse_recent(const char *json, RaTrophyList *out);

/// Recent unlocks for `user` within the last `minutes`. Needs a live network.
bool ra_fetch_recent(const char *user, const char *api_key, int minutes,
                     RaTrophyList *out);

void ra_badge_url(const char *badge, char *out, size_t out_size);
void ra_badge_path(const char *badge, const char *dir, char *out, size_t out_size);

/// Downloads the badge if it is not already cached. False if it cannot be had.
bool ra_ensure_badge(const char *badge, const char *dir);
