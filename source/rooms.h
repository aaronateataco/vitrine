#pragma once

#include "entry.h"

/*
 * Game Room definitions, loaded from franchises.json.
 *
 * A room carries a camera framing, the path to a model *on the user's own SD
 * card*, and the URL the model came from for attribution. VITRINE re-hosts no
 * models: most published models are not licensed for redistribution, and
 * routing around a download restriction would not change that. The link is
 * credit and a pointer, not a fetch target.
 *
 * Deliberately free of SDL and GL headers so it stays host-testable.
 */

enum {
    ROOM_NAME_LEN   = 64,
    ROOM_CREDIT_LEN = 160,
    ROOM_URL_LEN    = 256,
};

typedef struct {
    float yaw;        ///< Radians.
    float pitch;      ///< Radians.
    float distance;
    float target[3];
} RoomCamera;

typedef struct {
    char       match[ROOM_NAME_LEN];   ///< Shelf name this room belongs to.
    char       title[ROOM_NAME_LEN];   ///< Display name; falls back to match.
    char       model[ENTRY_PATH_LEN];  ///< Local .gltf/.glb path, may be empty.
    char       source[ROOM_URL_LEN];   ///< Where the model came from.
    char       credit[ROOM_CREDIT_LEN];///< Author and licence, shown in-room.
    RoomCamera camera;
} Room;

typedef struct {
    Room  *items;
    size_t count;
    size_t capacity;
} RoomList;

bool rooms_init(RoomList *rooms);
void rooms_free(RoomList *rooms);

/// Missing file is not an error; rooms then simply fall back to defaults.
Result rooms_load(RoomList *rooms, const char *path);

/// Writes a commented starter file describing the schema.
Result rooms_write_example(const char *path);

/// NULL when no room matches that shelf.
const Room *rooms_find(const RoomList *rooms, const char *shelf_name);

/// Framing used when a shelf has no room of its own.
void rooms_default_camera(RoomCamera *camera);
