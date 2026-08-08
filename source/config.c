#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"

#define DEFAULT_ARGS "\"{core}\" \"{rom}\""

/* Bounded copy: avoids the truncation diagnostics snprintf would raise here. */
static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim(char *text)
{
    while (is_space(*text))
        text++;

    char *end = text + strlen(text);
    while (end > text && is_space(end[-1]))
        end--;
    *end = '\0';

    return text;
}

bool systems_init(SystemList *systems)
{
    memset(systems, 0, sizeof(*systems));
    systems->capacity = 8;
    systems->items = calloc(systems->capacity, sizeof(*systems->items));
    return systems->items != NULL;
}

void systems_free(SystemList *systems)
{
    free(systems->items);
    memset(systems, 0, sizeof(*systems));
}

static System *systems_add(SystemList *systems)
{
    if (systems->count == systems->capacity) {
        size_t capacity = systems->capacity * 2;
        System *items = realloc(systems->items, capacity * sizeof(*items));
        if (!items)
            return NULL;
        systems->items = items;
        systems->capacity = capacity;
    }

    System *system = &systems->items[systems->count++];
    memset(system, 0, sizeof(*system));
    copy_str(system->args, sizeof(system->args), DEFAULT_ARGS);
    return system;
}

Result systems_load(SystemList *systems, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    char line[1024];
    System *current = NULL;

    while (fgets(line, sizeof(line), file)) {
        char *text = trim(line);

        if (*text == '\0' || *text == '#' || *text == ';')
            continue;

        if (*text == '[') {
            char *close = strchr(text, ']');
            if (!close)
                continue;
            *close = '\0';

            current = systems_add(systems);
            if (!current)
                break;
            copy_str(current->name, sizeof(current->name), trim(text + 1));
            continue;
        }

        if (!current)
            continue;

        char *equals = strchr(text, '=');
        if (!equals)
            continue;
        *equals = '\0';

        char *key = trim(text);
        char *value = trim(equals + 1);

        if (strcasecmp(key, "core") == 0)
            copy_str(current->core, sizeof(current->core), value);
        else if (strcasecmp(key, "roms") == 0) {
            /* Repeatable: one line per directory, so a system can pull from
               several libraries (e.g. both this app's layout and tico's). */
            if (*value && current->roms_count < SYSTEM_MAX_ROMS) {
                copy_str(current->roms[current->roms_count],
                         sizeof(current->roms[0]), value);
                current->roms_count++;
            }
        }
        else if (strcasecmp(key, "extensions") == 0)
            copy_str(current->extensions, sizeof(current->extensions), value);
        else if (strcasecmp(key, "args") == 0 && *value)
            copy_str(current->args, sizeof(current->args), value);
    }

    fclose(file);
    return 0;
}

Result systems_write_example(const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    fputs("# VITRINE systems configuration.\n"
          "#\n"
          "# One section per platform. VITRINE ships no cores and no ROMs - point\n"
          "# 'core' at a core NRO you built yourself, and 'roms' at your own dumps.\n"
          "#\n"
          "#   core        path to the core NRO\n"
          "#   roms        directory to scan, searched recursively.\n"
          "#               Repeat the key to scan several libraries.\n"
          "#   extensions  comma-separated, no dots\n"
          "#   args        optional; {core} and {rom} are substituted\n"
          "#               defaults to: " DEFAULT_ARGS "\n"
          "#\n"
          "# The sdmc:/tico paths match a stock tico install, whose cores are already\n"
          "# built as standalone NROs. Directories that do not exist are skipped, so\n"
          "# listing both layouts costs nothing. See examples/systems-tico.ini in the\n"
          "# repository for all twenty systems.\n"
          "\n"
          "[Nintendo Entertainment System]\n"
          "core = sdmc:/tico/cores/tico-fceumm.nro\n"
          "roms = sdmc:/roms/nes\n"
          "roms = sdmc:/tico/roms/nes\n"
          "extensions = nes, fds, unf, unif\n"
          "\n"
          "[Super Nintendo]\n"
          "core = sdmc:/tico/cores/tico-snes9x.nro\n"
          "roms = sdmc:/roms/snes\n"
          "roms = sdmc:/tico/roms/snes\n"
          "extensions = smc, sfc, fig, swc\n"
          "\n"
          "[Game Boy Advance]\n"
          "core = sdmc:/tico/cores/tico-mgba.nro\n"
          "roms = sdmc:/roms/gba\n"
          "roms = sdmc:/tico/roms/gba\n"
          "extensions = gba, agb\n"
          "\n"
          "[Nintendo 64]\n"
          "core = sdmc:/tico/cores/tico-mupen64plus.nro\n"
          "roms = sdmc:/roms/n64\n"
          "roms = sdmc:/tico/roms/n64\n"
          "extensions = n64, z64, v64\n"
          "\n"
          "[PlayStation]\n"
          "core = sdmc:/tico/cores/tico-duckstation.nro\n"
          "roms = sdmc:/roms/psx\n"
          "roms = sdmc:/tico/roms/psx\n"
          "extensions = cue, bin, chd, m3u, pbp, exe, iso\n",
          file);

    fclose(file);
    return 0;
}

bool system_matches(const System *system, const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0')
        return false;

    const char *extension = dot + 1;
    size_t extension_len = strlen(extension);
    const char *cursor = system->extensions;

    while (*cursor) {
        while (*cursor == ',' || is_space(*cursor))
            cursor++;
        if (!*cursor)
            break;

        const char *start = cursor;
        while (*cursor && *cursor != ',')
            cursor++;

        size_t len = (size_t)(cursor - start);
        while (len > 0 && is_space(start[len - 1]))
            len--;

        if (len == extension_len && strncasecmp(start, extension, len) == 0)
            return true;
    }

    return false;
}

bool system_expand_args(const System *system, const char *rom, char *out, size_t out_size)
{
    size_t written = 0;
    const char *cursor = system->args;

    while (*cursor) {
        const char *substitution = NULL;
        size_t token_len = 0;

        if (strncmp(cursor, "{core}", 6) == 0) {
            substitution = system->core;
            token_len = 6;
        } else if (strncmp(cursor, "{rom}", 5) == 0) {
            substitution = rom;
            token_len = 5;
        }

        if (substitution) {
            size_t len = strlen(substitution);
            if (written + len >= out_size)
                return false;
            memcpy(out + written, substitution, len);
            written += len;
            cursor += token_len;
        } else {
            if (written + 1 >= out_size)
                return false;
            out[written++] = *cursor++;
        }
    }

    out[written] = '\0';
    return true;
}
