#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <switch.h>

#include "diag.h"
#include "render.h"

#define TEXT_CACHE_SLOTS 96
#define TEXT_KEY_LEN     128

typedef struct {
    char         key[TEXT_KEY_LEN];
    TTF_Font    *font;
    SDL_Texture *texture;
    int          w, h;
    bool         used;
} TextEntry;

typedef struct {
    TextEntry slots[TEXT_CACHE_SLOTS];
    size_t    count;
} TextCache;

static void cache_flush(TextCache *cache);

/*
 * The system font lives in pl's shared memory. libnx hands back a pointer and a
 * length with no transformation, and FreeType consumes it directly - so nothing
 * needs decoding and no font has to be shipped with the app. The data is never
 * copied off the console.
 */
static PlFontData g_font_data;
static bool g_psm_ready = false;
static bool g_font_ready = false;

/* Rasterised at the real output size; upscaling 720p text is what makes a
   docked launcher look soft. */
static bool open_fonts(Render *render)
{
    int body_px = (int)(22.0f * render->scale + 0.5f);
    int head_px = (int)(30.0f * render->scale + 0.5f);

    if (render->font)       TTF_CloseFont(render->font);
    if (render->font_large) TTF_CloseFont(render->font_large);
    render->font = NULL;
    render->font_large = NULL;

    /* SDL takes ownership of neither RWops target; the memory is pl's. */
    SDL_RWops *body = SDL_RWFromConstMem(g_font_data.address, (int)g_font_data.size);
    SDL_RWops *head = SDL_RWFromConstMem(g_font_data.address, (int)g_font_data.size);
    if (!body || !head)
        return false;

    render->font = TTF_OpenFontRW(body, 1, body_px);
    render->font_large = TTF_OpenFontRW(head, 1, head_px);

    return render->font != NULL && render->font_large != NULL;
}

static bool load_shared_font(Render *render)
{
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc))
        return false;

    rc = plGetSharedFontByType(&g_font_data, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        plExit();
        return false;
    }

    g_font_ready = true;
    return open_fonts(render);
}

bool render_init(Render *render)
{
    memset(render, 0, sizeof(*render));

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;
    if (TTF_Init() != 0)
        return false;

    /*
     * Try progressively less demanding configurations. Requesting 4x MSAA
     * outright previously made window-surface creation fail on hardware, which
     * killed the app at startup - so nothing here is allowed to be mandatory.
     */
    static const struct {
        int  width;
        int  height;
        int  samples;
        const char *name;
    } attempts[] = {
        { 1920, 1080, 4, "1080p + 4x MSAA" },
        { 1920, 1080, 2, "1080p + 2x MSAA" },
        { 1920, 1080, 0, "1080p" },
        { 1280,  720, 0, "720p" },
    };

    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        if (render->window) {
            SDL_DestroyWindow(render->window);
            render->window = NULL;
        }

        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, attempts[i].samples ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, attempts[i].samples);

        render->window = SDL_CreateWindow("VITRINE", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          attempts[i].width, attempts[i].height, 0);
        if (!render->window) {
            diag_logf("window %s failed: %s", attempts[i].name, SDL_GetError());
            continue;
        }

        render->renderer = SDL_CreateRenderer(render->window, -1,
                                              SDL_RENDERER_ACCELERATED |
                                              SDL_RENDERER_PRESENTVSYNC);
        if (!render->renderer) {
            diag_logf("renderer %s failed: %s", attempts[i].name, SDL_GetError());
            continue;
        }

        diag_logf("video mode: %s", attempts[i].name);
        break;
    }

    if (!render->window || !render->renderer) {
        diag_logf("no usable video mode");
        return false;
    }

    SDL_SetRenderDrawBlendMode(render->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    render->width = SCREEN_W;
    render->height = SCREEN_H;
    render->scale = 1.0f;
    SDL_GetRendererOutputSize(render->renderer, &render->width, &render->height);
    if (render->height <= 0)
        render->height = SCREEN_H;
    render->scale = (float)render->height / (float)SCREEN_H;
    diag_logf("output %dx%d, scale %.3f", render->width, render->height,
              (double)render->scale);

    if (!load_shared_font(render))
        return false;

    render->text_cache = calloc(1, sizeof(TextCache));
    return render->text_cache != NULL;
}

void render_sync_output(Render *render)
{
    int width = render->width;
    int height = render->height;

    if (SDL_GetRendererOutputSize(render->renderer, &width, &height) != 0 || height <= 0)
        return;
    if (width == render->width && height == render->height)
        return;

    diag_logf("output size %dx%d -> %dx%d", render->width, render->height,
              width, height);

    render->width = width;
    render->height = height;
    render->scale = (float)height / (float)SCREEN_H;

    /* Every cached glyph run is the wrong size now. */
    cache_flush(render->text_cache);

    if (g_font_ready && !open_fonts(render))
        diag_logf("font reload failed at %dx%d", width, height);
}

/*
 * Everything above draws in a fixed 1280x720 space; this maps that onto the
 * real output. Text is the exception - it is rasterised at native pixels and
 * divided back down when drawn, so it stays sharp instead of being magnified.
 */
void render_system_status(char *clock, size_t clock_size, int *battery_percent,
                          bool *charging)
{
    if (clock && clock_size) {
        time_t now = time(NULL);
        struct tm local;

        if (localtime_r(&now, &local))
            strftime(clock, clock_size, "%H:%M", &local);
        else
            snprintf(clock, clock_size, "--:--");
    }

    if (battery_percent)
        *battery_percent = -1;
    if (charging)
        *charging = false;

    if (!g_psm_ready)
        return;

    u32 percent = 0;
    if (battery_percent && R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)))
        *battery_percent = (int)percent;

    PsmChargerType charger = PsmChargerType_Unconnected;
    if (charging && R_SUCCEEDED(psmGetChargerType(&charger)))
        *charging = charger != PsmChargerType_Unconnected;
}

void render_begin_frame(Render *render)
{
    SDL_RenderSetScale(render->renderer, render->scale, render->scale);
}

void render_exit(Render *render)
{
    TextCache *cache = render->text_cache;
    if (cache) {
        for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++)
            if (cache->slots[i].used && cache->slots[i].texture)
                SDL_DestroyTexture(cache->slots[i].texture);
        free(cache);
    }

    if (render->font)       TTF_CloseFont(render->font);
    if (render->font_large) TTF_CloseFont(render->font_large);
    if (render->renderer)   SDL_DestroyRenderer(render->renderer);
    if (render->window)     SDL_DestroyWindow(render->window);

    if (g_psm_ready) {
        psmExit();
        g_psm_ready = false;
    }

    TTF_Quit();
    SDL_Quit();
    plExit();
}

/*
 * Rasterising every visible label each frame is far too slow at 60Hz, so
 * results are memoised. The table is small and simply flushed when full, which
 * is fine because the working set is one screenful of labels.
 */
static TextEntry *cache_lookup(TextCache *cache, TTF_Font *font, const char *text)
{
    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        TextEntry *slot = &cache->slots[i];
        if (slot->used && slot->font == font && strcmp(slot->key, text) == 0)
            return slot;
    }
    return NULL;
}

static void cache_flush(TextCache *cache)
{
    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        if (cache->slots[i].used && cache->slots[i].texture)
            SDL_DestroyTexture(cache->slots[i].texture);
        memset(&cache->slots[i], 0, sizeof(cache->slots[i]));
    }
    cache->count = 0;
}

static TextEntry *cache_insert(Render *render, TTF_Font *font, const char *text)
{
    TextCache *cache = render->text_cache;

    if (cache->count == TEXT_CACHE_SLOTS)
        cache_flush(cache);

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, white);
    if (!surface)
        return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(render->renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);
    if (!texture)
        return NULL;

    for (size_t i = 0; i < TEXT_CACHE_SLOTS; i++) {
        TextEntry *slot = &cache->slots[i];
        if (slot->used)
            continue;

        snprintf(slot->key, sizeof(slot->key), "%s", text);
        slot->font = font;
        slot->texture = texture;
        slot->w = w;
        slot->h = h;
        slot->used = true;
        cache->count++;
        return slot;
    }

    SDL_DestroyTexture(texture);
    return NULL;
}

/* Textures are cached white and tinted on draw, so one raster serves any colour. */
static TextEntry *text_entry(Render *render, TTF_Font *font, const char *text)
{
    if (!text || !text[0])
        return NULL;

    TextEntry *slot = cache_lookup(render->text_cache, font, text);
    return slot ? slot : cache_insert(render, font, text);
}

void render_text(Render *render, TTF_Font *font, int x, int y,
                 SDL_Color color, const char *text)
{
    TextEntry *slot = text_entry(render, font, text);
    if (!slot)
        return;

    SDL_SetTextureColorMod(slot->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(slot->texture, color.a);

    SDL_Rect dst = { x, y, (int)(slot->w / render->scale),
                     (int)(slot->h / render->scale) };
    SDL_RenderCopy(render->renderer, slot->texture, NULL, &dst);
}

void render_text_fit(Render *render, TTF_Font *font, int x, int y, int width,
                     SDL_Color color, const char *text)
{
    if (!text || !text[0])
        return;

    TextEntry *slot = text_entry(render, font, text);
    if (!slot)
        return;

    SDL_SetTextureColorMod(slot->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(slot->texture, color.a);

    int w = (int)(slot->w / render->scale);
    int h = (int)(slot->h / render->scale);

    if (w <= width) {
        SDL_Rect dst = { x + (width - w) / 2, y, w, h };
        SDL_RenderCopy(render->renderer, slot->texture, NULL, &dst);
        return;
    }

    /* Too wide: show the leading portion rather than squashing the glyphs. */
    SDL_Rect src = { 0, 0, (int)(width * render->scale), slot->h };
    SDL_Rect dst = { x, y, width, h };
    SDL_RenderCopy(render->renderer, slot->texture, &src, &dst);
}

void render_text_measure(Render *render, TTF_Font *font, const char *text,
                         int *width, int *height)
{
    if (width)  *width = 0;
    if (height) *height = 0;

    TextEntry *slot = text_entry(render, font, text);
    if (!slot)
        return;

    /* Reported in design space, so callers can align without knowing the scale. */
    if (width)  *width = (int)(slot->w / render->scale);
    if (height) *height = (int)(slot->h / render->scale);
}

void render_text_right(Render *render, TTF_Font *font, int right, int y,
                       SDL_Color color, const char *text)
{
    int width = 0;
    render_text_measure(render, font, text, &width, NULL);
    render_text(render, font, right - width, y, color, text);
}

void render_fill(Render *render, SDL_Rect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(render->renderer, &rect);
}

void render_outline(Render *render, SDL_Rect rect, int thickness, SDL_Color color)
{
    SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; i < thickness; i++) {
        SDL_Rect r = { rect.x - i, rect.y - i, rect.w + 2 * i, rect.h + 2 * i };
        SDL_RenderDrawRect(render->renderer, &r);
    }
}

/*
 * Masks corners by overpainting them with the background. The boundary pixel is
 * blended by its fractional coverage rather than snapped to a whole pixel,
 * which is what stops the curve stair-stepping - very visible at 1080p.
 */
void render_round_corners(Render *render, SDL_Rect rect, int radius, SDL_Color bg)
{
    if (radius <= 0)
        return;

    /* Work in real pixels so the curve is smooth at any output scale. */
    float s = render->scale;
    SDL_FRect box = { rect.x * s, rect.y * s, rect.w * s, rect.h * s };
    float r = radius * s;

    SDL_RenderSetScale(render->renderer, 1.0f, 1.0f);

    for (int y = 0; y < (int)r; y++) {
        float dy = r - (float)y - 0.5f;
        float span = sqrtf(r * r - dy * dy);
        float inset = r - span;
        if (inset <= 0.0f)
            continue;

        int solid = (int)inset;
        float partial = inset - (float)solid;

        SDL_SetRenderDrawColor(render->renderer, bg.r, bg.g, bg.b, 255);
        if (solid > 0) {
            SDL_Rect spans[4] = {
                { (int)box.x,                       (int)box.y + y,                    solid, 1 },
                { (int)(box.x + box.w) - solid,     (int)box.y + y,                    solid, 1 },
                { (int)box.x,                       (int)(box.y + box.h) - 1 - y,      solid, 1 },
                { (int)(box.x + box.w) - solid,     (int)(box.y + box.h) - 1 - y,      solid, 1 },
            };
            SDL_RenderFillRects(render->renderer, spans, 4);
        }

        /* Feather the last pixel by how much of it the curve actually covers. */
        if (partial > 0.01f) {
            SDL_SetRenderDrawColor(render->renderer, bg.r, bg.g, bg.b,
                                   (Uint8)(partial * 255.0f));
            SDL_Rect edge[4] = {
                { (int)box.x + solid,                   (int)box.y + y,               1, 1 },
                { (int)(box.x + box.w) - solid - 1,     (int)box.y + y,               1, 1 },
                { (int)box.x + solid,                   (int)(box.y + box.h) - 1 - y, 1, 1 },
                { (int)(box.x + box.w) - solid - 1,     (int)(box.y + box.h) - 1 - y, 1, 1 },
            };
            SDL_RenderFillRects(render->renderer, edge, 4);
        }
    }

    SDL_RenderSetScale(render->renderer, s, s);
}

void render_shadow(Render *render, SDL_Rect rect, int spread)
{
    for (int i = spread; i > 0; i--) {
        /* Quadratic falloff reads as a soft shadow rather than a hard band. */
        float t = (float)i / (float)spread;
        Uint8 alpha = (Uint8)(70.0f * (1.0f - t) * (1.0f - t));

        SDL_SetRenderDrawColor(render->renderer, 0, 0, 0, alpha);
        SDL_Rect r = { rect.x - i, rect.y - i + 2, rect.w + 2 * i, rect.h + 2 * i };
        SDL_RenderDrawRect(render->renderer, &r);
    }
}

void render_edge_fade(Render *render, SDL_Rect rect, SDL_Color color, bool from_left)
{
    for (int x = 0; x < rect.w; x++) {
        float t = (float)x / (float)rect.w;
        if (!from_left)
            t = 1.0f - t;

        SDL_SetRenderDrawColor(render->renderer, color.r, color.g, color.b,
                               (Uint8)(color.a * (1.0f - t)));
        SDL_RenderDrawLine(render->renderer, rect.x + x, rect.y,
                           rect.x + x, rect.y + rect.h);
    }
}
