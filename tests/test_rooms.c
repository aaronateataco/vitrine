/* Host-side checks for franchises.json parsing and room lookup. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rooms.h"

#define ROOMS_FILE "testdata-rooms.json"

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

int main(void)
{
    RoomList rooms;
    if (!rooms_init(&rooms)) return 1;

    check("absent file is not fatal", R_FAILED(rooms_load(&rooms, ROOMS_FILE)));

    /* The written example must be parseable by the loader that emits it. */
    check("example written", R_SUCCEEDED(rooms_write_example(ROOMS_FILE)));
    check("example parses", R_SUCCEEDED(rooms_load(&rooms, ROOMS_FILE)));
    check("example defines one room", rooms.count == 1);
    check("example room matches a shelf",
          rooms_find(&rooms, "Game Boy Advance") != NULL);

    FILE *f = fopen(ROOMS_FILE, "w");
    if (!f) return 1;
    fputs("{\n"
          "  \"version\": 1,\n"
          "  \"rooms\": [\n"
          "    { \"match\": \"Nintendo 64\", \"title\": \"N64 Den\",\n"
          "      \"model\": \"sdmc:/models/n64.glb\",\n"
          "      \"source\": \"https://example.com/model\",\n"
          "      \"credit\": \"Someone, CC-BY\",\n"
          "      \"camera\": { \"yaw\": 1.25, \"pitch\": -0.5, \"distance\": 9.5,\n"
          "                  \"target\": [1.0, 2.0, 3.0] } },\n"
          "    { \"match\": \"Homebrew\" },\n"
          "    { \"title\": \"no match key, must be skipped\" }\n"
          "  ]\n"
          "}\n", f);
    fclose(f);

    check("load succeeds", R_SUCCEEDED(rooms_load(&rooms, ROOMS_FILE)));
    check("rooms without a match key are dropped", rooms.count == 2);

    const Room *n64 = rooms_find(&rooms, "Nintendo 64");
    check("room found by shelf name", n64 != NULL);
    if (n64) {
        check("title read", strcmp(n64->title, "N64 Den") == 0);
        check("model path read", strcmp(n64->model, "sdmc:/models/n64.glb") == 0);
        check("source url read", strcmp(n64->source, "https://example.com/model") == 0);
        check("credit read", strcmp(n64->credit, "Someone, CC-BY") == 0);
        check("camera yaw read", near(n64->camera.yaw, 1.25f));
        check("camera pitch read (negative)", near(n64->camera.pitch, -0.5f));
        check("camera distance read", near(n64->camera.distance, 9.5f));
        check("camera target read",
              near(n64->camera.target[0], 1.0f) &&
              near(n64->camera.target[1], 2.0f) &&
              near(n64->camera.target[2], 3.0f));
    }

    /* A room may omit everything but its match; defaults must fill in. */
    const Room *homebrew = rooms_find(&rooms, "Homebrew");
    check("sparse room still loads", homebrew != NULL);
    if (homebrew) {
        RoomCamera fallback;
        rooms_default_camera(&fallback);
        check("title defaults to the match name",
              strcmp(homebrew->title, "Homebrew") == 0);
        check("model may be empty", homebrew->model[0] == '\0');
        check("camera defaults applied",
              near(homebrew->camera.distance, fallback.distance) &&
              near(homebrew->camera.yaw, fallback.yaw));
    }

    check("unknown shelf has no room", rooms_find(&rooms, "Dreamcast") == NULL);
    check("null shelf name is safe", rooms_find(&rooms, NULL) == NULL);

    /* Corrupt input must be rejected rather than half-applied. */
    f = fopen(ROOMS_FILE, "w");
    if (f) { fputs("{ not json at all", f); fclose(f); }
    check("corrupt file rejected", R_FAILED(rooms_load(&rooms, ROOMS_FILE)));
    check("corrupt file leaves no rooms", rooms.count == 0);

    rooms_free(&rooms);
    remove(ROOMS_FILE);

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
