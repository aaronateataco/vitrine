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

Result overrides_load(OverrideList *overrides, const char *path)
{
    overrides->count = 0;
    overrides->dirty = false;

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

    cJSON *entries = cJSON_AddObjectToObject(root, "entries");
    if (!entries) {
        cJSON_Delete(root);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    for (size_t i = 0; i < overrides->count; i++) {
        const Override *slot = &overrides->items[i];

        /* Records back at their defaults carry no information; drop them. */
        if (!slot->hidden && !slot->promote)
            continue;

        cJSON *node = cJSON_AddObjectToObject(entries, slot->key);
        if (!node)
            continue;

        if (slot->hidden)
            cJSON_AddBoolToObject(node, "hidden", true);
        if (slot->promote)
            cJSON_AddBoolToObject(node, "promote", true);
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
