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

/// Measures without drawing, so callers can right-align or wrap.
void render_text_measure(Render *render, TTF_Font *font, const char *text,
                         int *width, int *height);

/// Draws with the text's right edge at `right`, keeping it on screen.
void render_text_right(Render *render, TTF_Font *font, int right, int y,
                       SDL_Color color, const char *text);

void render_fill(Render *render, SDL_Rect rect, SDL_Color color);
void render_outline(Render *render, SDL_Rect rect, int thickness, SDL_Color color);

/// Soft drop shadow: concentric rings of decreasing alpha around `rect`.
void render_shadow(Render *render, SDL_Rect rect, int spread);

/// Horizontal fade used to hint that a shelf continues past the screen edge.
void render_edge_fade(Render *render, SDL_Rect rect, SDL_Color color, bool from_left);
