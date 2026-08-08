#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "net.h"
#include "ra.h"
#include "vendor/cJSON.h"

#define RA_API   "https://retroachievements.org/API"
#define RA_MEDIA "https://media.retroachievements.org/Badge"

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
    if (cJSON_IsString(item) && item->valuestring)
        return item->valuestring;
    return "";
}

/* The API returns some numeric fields as strings, so accept either. */
static int json_number(const cJSON *node, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (cJSON_IsNumber(item))
        return item->valueint;
    if (cJSON_IsString(item) && item->valuestring)
        return atoi(item->valuestring);
    return 0;
}

bool ra_parse_recent(const char *json, RaTrophyList *out)
{
    if (!json || !out)
        return false;

    out->count = 0;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    /* The endpoint returns a bare array. */
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *node = NULL;
    cJSON_ArrayForEach(node, root) {
        if (out->count == RA_MAX_TROPHIES)
            break;
        if (!cJSON_IsObject(node))
            continue;

        const char *badge = json_string(node, "BadgeName");
        const char *title = json_string(node, "Title");
        if (!badge[0] || !title[0])
            continue;   /* Without both there is nothing to show on a medal. */

        RaTrophy *trophy = &out->items[out->count++];
        memset(trophy, 0, sizeof(*trophy));
        copy_str(trophy->title, sizeof(trophy->title), title);
        copy_str(trophy->game, sizeof(trophy->game), json_string(node, "GameTitle"));
        copy_str(trophy->badge, sizeof(trophy->badge), badge);
        trophy->points = json_number(node, "Points");
    }

    cJSON_Delete(root);
    return true;
}

bool ra_fetch_recent(const char *user, const char *api_key, int minutes,
                     RaTrophyList *out)
{
    if (!user || !user[0] || !api_key || !api_key[0] || !out)
        return false;

    char encoded_user[128];
    char encoded_key[256];
    net_urlencode(user, encoded_user, sizeof(encoded_user));
    net_urlencode(api_key, encoded_key, sizeof(encoded_key));

    char url[768];
    snprintf(url, sizeof(url),
             RA_API "/API_GetUserRecentAchievements.php?z=%s&y=%s&u=%s&m=%d",
             encoded_user, encoded_key, encoded_user, minutes);

    char *body = NULL;
    if (!net_get(url, NULL, &body, NULL))
        return false;

    bool ok = ra_parse_recent(body, out);
    free(body);
    return ok;
}

void ra_badge_url(const char *badge, char *out, size_t out_size)
{
    snprintf(out, out_size, RA_MEDIA "/%s.png", badge ? badge : "");
}

void ra_badge_path(const char *badge, const char *dir, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s.png", dir, badge ? badge : "unknown");
}

bool ra_ensure_badge(const char *badge, const char *dir)
{
    if (!badge || !badge[0] || !dir)
        return false;

    char path[512];
    ra_badge_path(badge, dir, path, sizeof(path));

    FILE *probe = fopen(path, "rb");
    if (probe) {
        fclose(probe);
        return true;   /* Already cached. */
    }

    mkdir(dir, 0777);

    char url[RA_URL_LEN];
    ra_badge_url(badge, url, sizeof(url));
    return net_download(url, path);
}
