#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_image.h>

#include "diag.h"
#include "user.h"

/* Profile images are small JPEGs; this is comfortably above the real size. */
#define AVATAR_MAX_BYTES (256 * 1024)

static SDL_Texture *load_avatar(AccountProfile *profile, SDL_Renderer *renderer)
{
    u32 size = 0;
    if (R_FAILED(accountProfileGetImageSize(profile, &size)) || size == 0 ||
        size > AVATAR_MAX_BYTES)
        return NULL;

    void *buffer = malloc(size);
    if (!buffer)
        return NULL;

    u32 written = 0;
    if (R_FAILED(accountProfileLoadImage(profile, buffer, size, &written)) ||
        written == 0) {
        free(buffer);
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(buffer, (int)written);
    SDL_Texture *texture = NULL;

    if (rw) {
        SDL_Surface *surface = IMG_Load_RW(rw, 1);
        if (surface) {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    free(buffer);
    return texture;
}

bool user_select(User *user, SDL_Renderer *renderer)
{
    memset(user, 0, sizeof(*user));

    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) {
        diag_logf("accountInitialize failed: 0x%x", rc);
        return false;
    }

    PselUserSelectionSettings settings;
    memset(&settings, 0, sizeof(settings));

    rc = pselShowUserSelector(&user->uid, &settings);
    if (R_FAILED(rc)) {
        /* Backing out of the picker is a normal choice, not an error. */
        diag_logf("user selection dismissed: 0x%x", rc);
        accountExit();
        return false;
    }

    AccountProfile profile;
    if (R_SUCCEEDED(accountGetProfile(&profile, user->uid))) {
        AccountProfileBase base;
        memset(&base, 0, sizeof(base));

        if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base)))
            snprintf(user->nickname, sizeof(user->nickname), "%s", base.nickname);

        user->avatar = load_avatar(&profile, renderer);
        accountProfileClose(&profile);
    }

    user->valid = true;
    diag_logf("user selected: \"%s\" (avatar %s)",
              user->nickname[0] ? user->nickname : "?",
              user->avatar ? "loaded" : "unavailable");

    accountExit();
    return true;
}

void user_free(User *user)
{
    if (user->avatar) {
        SDL_DestroyTexture(user->avatar);
        user->avatar = NULL;
    }
    user->valid = false;
}
