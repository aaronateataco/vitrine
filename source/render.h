#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "entry.h"

enum {
    SCREEN_W = 1280,
    SCREEN_H = 720,
};

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font;        ///< Body text.
    TTF_Font     *font_large;  ///< Headings and the selected title.
    void         *text_cache;  ///< Opaque; see render.c.
} Render;

bool render_init(Render *render);
void render_exit(Render *render);

/// Rasterised strings are cached, so calling these every frame is cheap.
void render_text(Render *render, TTF_Font *font, int x, int y,
                 SDL_Color color, const char *text);

/// Draws centred within [x, x+width), truncating with an ellipsis if needed.
void render_text_fit(Render *render, TTF_Font *font, int x, int y, int width,
                     SDL_Color color, const char *text);

void render_fill(Render *render, SDL_Rect rect, SDL_Color color);
void render_outline(Render *render, SDL_Rect rect, int thickness, SDL_Color color);
