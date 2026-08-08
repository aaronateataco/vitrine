#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overrides.h"
#include "vendor/cJSON.h"

#define CONFIG_VERSION 1

bool overrides_init(OverrideList *overrides)
{
    memset(overrides, 0, sizeof(*overrides));
    overrides->capacity = 16;
    overrides->items = calloc(overrides->capacity, sizeof(*overrides->items));
    return overrides->items != NULL;
}

void overrides_free(OverrideList *overrides)
{
    free(overrides->items);
    memset(overrides, 0, sizeof(*overrides));
}

/*
 * Titles are identified by application id because their storage path is an
 * implementation detail the user never sees and which can change.
 */
void overrides_key(const Entry *entry, char *out, size_t out_size)
{
    if (entry->kind == EntryKind_Title)
        snprintf(out, out_size, "title:%016lX", (unsigned long)entry->application_id);
    else
        snprintf(out, out_size, "%s", entry->path);
}

static Override *find_mutable(OverrideList *overrides, const char *key)
{
    for (size_t i = 0; i < overrides->count; i++)
        if (strcmp(overrides->items[i].key, key) == 0)
            return &overrides->items[i];
    return NULL;
}

const Override *overrides_find(const OverrideList *overrides, const char *key)
{
    for (size_t i = 0; i < overrides->count; i++)
        if (strcmp(overrides->items[i].key, key) == 0)
            return &overrides->items[i];
    return NULL;
}

static Override *ensure(OverrideList *overrides, const char *key)
{
    Override *existing = find_mutable(overrides, key);
    if (existing)
        return existing;

    if (overrides->count == overrides->capacity) {
        size_t capacity = overrides->capacity * 2;
        Override *items = realloc(overrides->items, capacity * sizeof(*items));
        if (!items)
            return NULL;
        overrides->items = items;
        overrides->capacity = capacity;
    }

    Override *slot = &overrides->items[overrides->count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    return slot;
}

bool overrides_hidden(const OverrideList *overrides, const Entry *entry)
{
    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    const Override *found = overrides_find(overrides, key);
    return found && found->hidden;
}

bool overrides_promoted(const OverrideList *overrides, const Entry *entry)
{
    /* Only homebrew can be re-tagged; anything else would be meaningless. */
    if (entry->kind != EntryKind_Homebrew)
        return false;

    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    const Override *found = overrides_find(overrides, key);
    return found && found->promote;
}

void overrides_toggle_hidden(OverrideList *overrides, const Entry *entry)
{
    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    Override *slot = ensure(overrides, key);
    if (!slot)
        return;

    slot->hidden = !slot->hidden;
    overrides->dirty = true;
}

void overrides_toggle_promote(OverrideList *overrides, const Entry *entry)
{
    if (entry->kind != EntryKind_Homebrew)
        return;

    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    Override *slot = ensure(overrides, key);
    if (!slot)
        return;

    slot->promote = !slot->promote;
    overrides->dirty = true;
}

static void shelf_key(const char *shelf_name, char *out, size_t out_size)
{
    snprintf(out, out_size, "shelf:%s", shelf_name);
}

bool overrides_shelf_hidden(const OverrideList *overrides, const char *shelf_name)
{
    char key[ENTRY_PATH_LEN];
    shelf_key(shelf_name, key, sizeof(key));

    const Override *found = overrides_find(overrides, key);
    return found && found->hidden;
}

void overrides_toggle_shelf(OverrideList *overrides, const char *shelf_name)
{
    char key[ENTRY_PATH_LEN];
    shelf_key(shelf_name, key, sizeof(key));

    Override *slot = ensure(overrides, key);
    if (!slot)
        return;

    slot->hidden = !slot->hidden;
    overrides->dirty = true;
}

void overrides_set_cover(OverrideList *overrides, const Entry *entry,
                         const char *url)
{
    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    Override *slot = ensure(overrides, key);
    if (!slot)
        return;

    snprintf(slot->cover_url, sizeof(slot->cover_url), "%s", url ? url : "");
    overrides->dirty = true;
}

const char *overrides_cover(const OverrideList *overrides, const Entry *entry)
{
    char key[ENTRY_PATH_LEN];
    overrides_key(entry, key, sizeof(key));

    const Override *found = overrides_find(overrides, key);
    return found ? found->cover_url : "";
}

void overrides_unhide_all(OverrideList *overrides)
{
    for (size_t i = 0; i < overrides->count; i++)
        overrides->items[i].hidden = false;
    overrides->dirty = true;
}

Result overrides_load(OverrideList *overrides, const char *path)
{
    overrides->count = 0;
    overrides->dirty = false;
    memset(&overrides->prefs, 0, sizeof(overrides->prefs));

    FILE *file = fopen(path, "rb");
    if (!file)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    /* A config this large is corrupt; refuse rather than allocate wildly. */
    if (size <= 0 || size > 4 * 1024 * 1024) {
        fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    size_t read = fread(text, 1, (size_t)size, file);
    text[read] = '\0';
    fclose(file);

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    cJSON *prefs = cJSON_GetObjectItemCaseSensitive(root, "prefs");
    if (cJSON_IsObject(prefs)) {
        overrides->prefs.show_hidden =
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(prefs, "show_hidden"));
        overrides->prefs.poster_tiles =
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(prefs, "poster_tiles"));
        /* large_tiles was a bool before cover_size existed; honour it once. */
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(prefs, "large_tiles")))
            overrides->prefs.cover_size = 1;

        cJSON *size = cJSON_GetObjectItemCaseSensitive(prefs, "cover_size");
        if (cJSON_IsNumber(size) && size->valueint >= 0 && size->valueint <= 2)
            overrides->prefs.cover_size = size->valueint;

        cJSON *key = cJSON_GetObjectItemCaseSensitive(prefs, "steamgriddb_key");
        if (cJSON_IsString(key) && key->valuestring)
            snprintf(overrides->prefs.sgdb_key, sizeof(overrides->prefs.sgdb_key),
                     "%s", key->valuestring);

        cJSON *theme = cJSON_GetObjectItemCaseSensitive(prefs, "theme");
        if (cJSON_IsNumber(theme) && theme->valueint >= 0)
            overrides->prefs.theme = theme->valueint;
    }

    cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (cJSON_IsObject(entries)) {
        cJSON *node = NULL;
        cJSON_ArrayForEach(node, entries) {
            if (!node->string || !cJSON_IsObject(node))
                continue;

            Override *slot = ensure(overrides, node->string);
            if (!slot)
                break;

            cJSON *hidden = cJSON_GetObjectItemCaseSensitive(node, "hidden");
            cJSON *promote = cJSON_GetObjectItemCaseSensitive(node, "promote");
            slot->hidden = cJSON_IsTrue(hidden);
            slot->promote = cJSON_IsTrue(promote);

            cJSON *cover = cJSON_GetObjectItemCaseSensitive(node, "cover");
            if (cJSON_IsString(cover) && cover->valuestring)
                snprintf(slot->cover_url, sizeof(slot->cover_url), "%s",
                         cover->valuestring);
        }
    }

    cJSON_Delete(root);
    return 0;
}

Result overrides_save(OverrideList *overrides, const char *path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    cJSON_AddNumberToObject(root, "version", CONFIG_VERSION);

    cJSON *prefs = cJSON_AddObjectToObject(root, "prefs");
    if (prefs) {
        cJSON_AddBoolToObject(prefs, "show_hidden", overrides->prefs.show_hidden);
        cJSON_AddBoolToObject(prefs, "poster_tiles", overrides->prefs.poster_tiles);
        cJSON_AddNumberToObject(prefs, "cover_size", overrides->prefs.cover_size);
        if (overrides->prefs.sgdb_key[0])
            cJSON_AddStringToObject(prefs, "steamgriddb_key", overrides->prefs.sgdb_key);
        cJSON_AddNumberToObject(prefs, "theme", overrides->prefs.theme);
    }

    cJSON *entries = cJSON_AddObjectToObject(root, "entries");
    if (!entries) {
        cJSON_Delete(root);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    for (size_t i = 0; i < overrides->count; i++) {
        const Override *slot = &overrides->items[i];

        /* Records back at their defaults carry no information; drop them. */
        if (!slot->hidden && !slot->promote && !slot->cover_url[0])
            continue;

        cJSON *node = cJSON_AddObjectToObject(entries, slot->key);
        if (!node)
            continue;

        if (slot->hidden)
            cJSON_AddBoolToObject(node, "hidden", true);
        if (slot->promote)
            cJSON_AddBoolToObject(node, "promote", true);
        if (slot->cover_url[0])
            cJSON_AddStringToObject(node, "cover", slot->cover_url);
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

    FILE *file = fopen(path, "wb");
    if (!file) {
        free(text);
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    size_t length = strlen(text);
    bool ok = fwrite(text, 1, length, file) == length;
    fclose(file);
    free(text);

    if (!ok)
        return MAKERESULT(Module_Libnx, LibnxError_IoError);

    overrides->dirty = false;
    return 0;
}
