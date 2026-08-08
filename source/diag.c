#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL2/SDL_image.h>
#include <switch.h>

#include "diag.h"

static FILE *g_log = NULL;

void diag_open(const char *path)
{
    g_log = fopen(path, "w");
    if (!g_log)
        return;

    diag_logf("VITRINE diagnostics log");
    diag_logf("built %s %s", __DATE__, __TIME__);
}

void diag_close(void)
{
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void diag_logf(const char *fmt, ...)
{
    if (!g_log)
        return;

    /* Seconds since start is more useful here than a wall clock. */
    fprintf(g_log, "[%8.3f] ", (double)armTicksToNs(armGetSystemTick()) / 1.0e9);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);

    fputc('\n', g_log);
    fflush(g_log);
}

bool diag_screenshot(SDL_Renderer *renderer, const char *dir, char *out_path,
                     size_t out_path_size)
{
    int w = 0;
    int h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0)
        return false;

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                          SDL_PIXELFORMAT_ABGR8888);
    if (!surface)
        return false;

    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ABGR8888,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return false;
    }

    mkdir(dir, 0777);

    /* Find a free slot rather than overwriting; sessions produce several. */
    char path[512];
    bool named = false;
    for (int i = 0; i < 999; i++) {
        snprintf(path, sizeof(path), "%s/vitrine-%03d.png", dir, i);
        FILE *probe = fopen(path, "rb");
        if (!probe) {
            named = true;
            break;
        }
        fclose(probe);
    }

    bool ok = named && IMG_SavePNG(surface, path) == 0;
    SDL_FreeSurface(surface);

    if (ok) {
        diag_logf("screenshot saved: %s (%dx%d)", path, w, h);
        if (out_path)
            snprintf(out_path, out_path_size, "%s", path);
    } else {
        diag_logf("screenshot FAILED: %s", SDL_GetError());
    }

    return ok;
}

static const char *applet_type_name(void)
{
    switch (appletGetAppletType()) {
        case AppletType_Application:       return "Application (title takeover)";
        case AppletType_SystemApplication: return "SystemApplication";
        case AppletType_LibraryApplet:     return "LibraryApplet (album takeover)";
        case AppletType_OverlayApplet:     return "OverlayApplet";
        case AppletType_SystemApplet:      return "SystemApplet";
        default:                           return "None/Default";
    }
}

/*
 * hbloader returns here when a program exits without naming a successor, so
 * whatever this file is determines where the console lands after a game. If a
 * frontend has installed itself here, no launcher-side change can override it.
 */
static void report_hbmenu(FILE *out)
{
    static const char *path = "sdmc:/hbmenu.nro";

    fprintf(out, "\n== hbloader return target ==\n");

    FILE *probe = fopen(path, "rb");
    if (!probe) {
        fprintf(out, "%s: MISSING\n", path);
        return;
    }

    fseek(probe, 0, SEEK_END);
    long size = ftell(probe);
    fclose(probe);

    char name[0x200] = { 0 };
    char author[0x100] = { 0 };

    if (nro_read_metadata(path, name, sizeof(name), author, sizeof(author)))
        fprintf(out, "%s: \"%s\" by \"%s\" (%ld bytes)\n", path, name, author, size);
    else
        fprintf(out, "%s: present, no NACP metadata (%ld bytes)\n", path, size);

    fprintf(out, "This is where the console goes when a game exits.\n");
}

static void report_systems(FILE *out, const SystemList *systems)
{
    fprintf(out, "\n== systems (%zu) ==\n", systems->count);

    for (size_t i = 0; i < systems->count; i++) {
        const System *system = &systems->items[i];
        const char *core = system_pick_core(system);

        fprintf(out, "\n[%s]\n", system->name);
        fprintf(out, "  extensions : %s\n", system->extensions);
        fprintf(out, "  args       : %s\n", system->args);

        for (size_t c = 0; c < system->core_count; c++) {
            FILE *probe = fopen(system->core[c], "rb");
            bool present = probe != NULL;
            if (probe)
                fclose(probe);

            fprintf(out, "  core[%zu]    : %s  [%s]%s\n", c, system->core[c],
                    present ? "present" : "missing",
                    (core && strcmp(core, system->core[c]) == 0) ? "  <- USED" : "");
        }

        if (!core)
            fprintf(out, "  !! no usable core; ROMs for this system will not launch\n");

        for (size_t r = 0; r < system->roms_count; r++)
            fprintf(out, "  roms[%zu]    : %s\n", r, system->roms[r]);
    }
}

bool diag_write_report(const char *path, const EntryList *list,
                       const SystemList *systems, const ShelfList *shelves,
                       const OverrideList *overrides)
{
    FILE *out = fopen(path, "w");
    if (!out)
        return false;

    fprintf(out, "VITRINE diagnostics report\n");
    fprintf(out, "built %s %s\n", __DATE__, __TIME__);

    fprintf(out, "\n== environment ==\n");
    fprintf(out, "applet type    : %s\n", applet_type_name());
    fprintf(out, "firmware       : %u.%u.%u\n", hosversionGet() >> 16 & 0xff,
            hosversionGet() >> 8 & 0xff, hosversionGet() & 0xff);
    fprintf(out, "next-load avail: %s\n", envHasNextLoad() ? "yes" : "no");

    fprintf(out, "\n== preferences ==\n");
    fprintf(out, "theme        : %d\n", overrides->prefs.theme);
    fprintf(out, "poster tiles : %s\n", overrides->prefs.poster_tiles ? "yes" : "no");
    fprintf(out, "large tiles  : %s\n", overrides->prefs.large_tiles ? "yes" : "no");
    fprintf(out, "show hidden  : %s\n", overrides->prefs.show_hidden ? "yes" : "no");
    fprintf(out, "overrides    : %zu recorded\n", overrides->count);

    size_t homebrew = 0;
    size_t games = 0;
    size_t titles = 0;
    for (size_t i = 0; i < list->count; i++) {
        switch (list->items[i].kind) {
            case EntryKind_Homebrew: homebrew++; break;
            case EntryKind_Game:     games++;    break;
            default:                 titles++;   break;
        }
    }

    fprintf(out, "\n== library ==\n");
    fprintf(out, "entries : %zu (%zu homebrew, %zu roms, %zu installed)\n",
            list->count, homebrew, games, titles);

    fprintf(out, "\n== shelves (%zu) ==\n", shelves->count);
    for (size_t i = 0; i < shelves->count; i++)
        fprintf(out, "  %-32s %4zu items%s\n", shelves->items[i].name,
                shelves->items[i].count,
                shelves->items[i].hidden ? "  (hidden)" : "");

    report_systems(out, systems);
    report_hbmenu(out);

    fprintf(out, "\n== overrides ==\n");
    for (size_t i = 0; i < overrides->count; i++)
        fprintf(out, "  %s%s%s\n", overrides->items[i].key,
                overrides->items[i].hidden ? "  hidden" : "",
                overrides->items[i].promote ? "  promote" : "");

    fclose(out);
    diag_logf("diagnostics report written: %s", path);
    return true;
}
