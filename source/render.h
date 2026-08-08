#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "entry.h"

/*
 * Layout is authored against this virtual size and scaled to whatever the
 * console is actually outputting: 720p handheld, 1080p docked. Fonts are
 * rasterised at the real pixel size rather than upscaled, so docked output is
 * genuinely sharp instead of a magnified 720p image.
 */
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

    int    width;              ///< Real output width in pixels.
    int    height;
    float  scale;              ///< height / SCREEN_H.
} Render;

/*
 * Re-reads the output size. When it changes - docking or undocking - fonts are
 * rebuilt at the new pixel size and the text cache is dropped, since every
 * cached glyph run is now the wrong size.
 */
void render_sync_output(Render *render);

/*
 * Call once per frame before drawing. Scales the coordinate space so all layout
 * stays authored in SCREEN_W x SCREEN_H while output is native resolution.
 */
void render_begin_frame(Render *render);

/*
 * Clock and battery for the console layout's status bar, mirroring what the
 * real HOME menu shows. Battery is -1 when psm is unavailable.
 */
void render_system_status(char *clock, size_t clock_size, int *battery_percent,
                          bool *charging);

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

/*
 * Masks the corners of the last thing drawn into `rect` by overpainting them
 * with `bg`. SDL2 has no rounded primitive, and Switch 2 rounds its game icons.
 */
void render_round_corners(Render *render, SDL_Rect rect, int radius, SDL_Color bg);

/// Soft drop shadow: concentric rings of decreasing alpha around `rect`.
void render_shadow(Render *render, SDL_Rect rect, int spread);

/// Horizontal fade used to hint that a shelf continues past the screen edge.
void render_edge_fade(Render *render, SDL_Rect rect, SDL_Color color, bool from_left);
