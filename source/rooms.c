#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rooms.h"
#include "vendor/cJSON.h"

#define ROOMS_VERSION 1

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (len >= dst_size)
        len = dst_size - 1;
    if (len)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

static const char *json_string(const cJSON *node, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static float json_float(const cJSON *node, const char *key, float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

void rooms_default_camera(RoomCamera *camera)
{
    camera->yaw = 0.6f;
    camera->pitch = 0.35f;
    camera->distance = 6.0f;
    camera->target[0] = camera->target[1] = camera->target[2] = 0.0f;
}

bool rooms_init(RoomList *rooms)
{
    memset(rooms, 0, sizeof(*rooms));
    rooms->capacity = 8;
    rooms->items = calloc(rooms->capacity, sizeof(*rooms->items));
    return rooms->items != NULL;
}

void rooms_free(RoomList *rooms)
{
    free(rooms->items);
    memset(rooms, 0, sizeof(*rooms));
}

static Room *rooms_add(RoomList *rooms)
{
    if (rooms->count == rooms->capacity) {
        size_t capacity = rooms->capacity * 2;
        Room *items = realloc(rooms->items, capacity * sizeof(*items));
        if (!items)
            return NULL;
        rooms->items = items;
        rooms->capacity = capacity;
    }

    Room *room = &rooms->items[rooms->count++];
    memset(room, 0, sizeof(*room));
    rooms_default_camera(&room->camera);
    return room;
}

Result rooms_load(RoomList *rooms, const char *path)
{
    rooms->count = 0;

    FILE *file = fopen(path, "rb");
    if (!file)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    if (size <= 0 || size > 1024 * 1024) {
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

    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "rooms");
    if (cJSON_IsArray(list)) {
        cJSON *node = NULL;
        cJSON_ArrayForEach(node, list) {
            if (!cJSON_IsObject(node))
                continue;

            const char *match = json_string(node, "match");
            if (!match[0])
                continue;   /* Without a shelf to attach to, a room is unreachable. */

            Room *room = rooms_add(rooms);
            if (!room)
                break;

            copy_str(room->match, sizeof(room->match), match);

            const char *title = json_string(node, "title");
            copy_str(room->title, sizeof(room->title), title[0] ? title : match);

            copy_str(room->model, sizeof(room->model), json_string(node, "model"));
            copy_str(room->source, sizeof(room->source), json_string(node, "source"));
            copy_str(room->credit, sizeof(room->credit), json_string(node, "credit"));

            cJSON *camera = cJSON_GetObjectItemCaseSensitive(node, "camera");
            if (cJSON_IsObject(camera)) {
                room->camera.yaw = json_float(camera, "yaw", room->camera.yaw);
                room->camera.pitch = json_float(camera, "pitch", room->camera.pitch);
                room->camera.distance =
                    json_float(camera, "distance", room->camera.distance);

                cJSON *target = cJSON_GetObjectItemCaseSensitive(camera, "target");
                if (cJSON_IsArray(target) && cJSON_GetArraySize(target) == 3)
                    for (int i = 0; i < 3; i++) {
                        cJSON *component = cJSON_GetArrayItem(target, i);
                        if (cJSON_IsNumber(component))
                            room->camera.target[i] = (float)component->valuedouble;
                    }
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

const Room *rooms_find(const RoomList *rooms, const char *shelf_name)
{
    if (!shelf_name)
        return NULL;

    for (size_t i = 0; i < rooms->count; i++)
        if (strcmp(rooms->items[i].match, shelf_name) == 0)
            return &rooms->items[i];

    return NULL;
}

Result rooms_write_example(const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);

    fputs("{\n"
          "  \"version\": 1,\n"
          "\n"
          "  \"_readme\": [\n"
          "    \"One entry per shelf you want a 3D room for.\",\n"
          "    \"match    - shelf name exactly as it appears in the library\",\n"
          "    \"model    - path to a .gltf or .glb ON YOUR OWN SD CARD\",\n"
          "    \"source   - where you got the model; shown as attribution\",\n"
          "    \"credit   - author and licence, shown in the room\",\n"
          "    \"camera   - yaw and pitch in radians, distance, and a target point\",\n"
          "    \"VITRINE downloads no models. Most published models are not\",\n"
          "    \"licensed for redistribution, so supply your own file and keep\",\n"
          "    \"the source link for credit.\"\n"
          "  ],\n"
          "\n"
          "  \"rooms\": [\n"
          "    {\n"
          "      \"match\": \"Game Boy Advance\",\n"
          "      \"title\": \"GBA Room\",\n"
          "      \"model\": \"sdmc:/switch/vitrine/models/gba-room.glb\",\n"
          "      \"source\": \"https://sketchfab.com/3d-models/example\",\n"
          "      \"credit\": \"Model by Example Author, CC-BY 4.0\",\n"
          "      \"camera\": { \"yaw\": 0.6, \"pitch\": 0.3, \"distance\": 6.0,\n"
          "                  \"target\": [0.0, 1.0, 0.0] }\n"
          "    }\n"
          "  ]\n"
          "}\n",
          file);

    fclose(file);
    return 0;
}
