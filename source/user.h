#pragma once

#include <SDL2/SDL.h>
#include <switch.h>

/*
 * The console's own account picker. Asking on launch matches what a Switch
 * title does, and gives us the real profile picture and nickname for the
 * top-left corner instead of a placeholder.
 */
typedef struct {
    AccountUid   uid;
    bool         valid;
    char         nickname[0x20];
    SDL_Texture *avatar;      ///< NULL when the profile image cannot be loaded.
} User;

/*
 * Shows the system user selector. Returns false if the user backs out or the
 * account services are unavailable, in which case the app carries on without a
 * profile rather than refusing to start.
 */
bool user_select(User *user, SDL_Renderer *renderer);

void user_free(User *user);
