/* Host-side checks for RetroAchievements response parsing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra.h"

/*
 * ra.c calls into net.c for live requests. Only the parser is exercised here,
 * so these stubs satisfy the linker without pulling in curl.
 */
bool net_get(const char *url, const char *bearer, char **out, size_t *out_len)
{
    (void)url; (void)bearer; (void)out; (void)out_len;
    return false;
}

bool net_download(const char *url, const char *path)
{
    (void)url; (void)path;
    return false;
}

char *net_urlencode(const char *text, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", text ? text : "");
    return out;
}

static int failures = 0;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int main(void)
{
    RaTrophyList list;

    /* The real endpoint returns a bare array of objects. */
    static const char *good =
        "[{\"AchievementID\":1,\"Title\":\"First Blood\","
        "\"Description\":\"Win a fight\",\"BadgeName\":\"12345\","
        "\"GameTitle\":\"Some Game\",\"Points\":10},"
        "{\"AchievementID\":2,\"Title\":\"Collector\","
        "\"BadgeName\":\"67890\",\"GameTitle\":\"Other Game\",\"Points\":\"25\"}]";

    check("valid response parses", ra_parse_recent(good, &list));
    check("two trophies read", list.count == 2);
    check("title read", strcmp(list.items[0].title, "First Blood") == 0);
    check("game read", strcmp(list.items[0].game, "Some Game") == 0);
    check("badge id read", strcmp(list.items[0].badge, "12345") == 0);
    check("numeric points read", list.items[0].points == 10);

    /* The API is inconsistent about quoting numbers, so both must work. */
    check("string-encoded points read", list.items[1].points == 25);

    /* Entries missing a badge or title cannot be drawn on a medal. */
    static const char *partial =
        "[{\"Title\":\"No Badge\",\"Points\":5},"
        "{\"BadgeName\":\"999\",\"Points\":5},"
        "{\"Title\":\"Fine\",\"BadgeName\":\"111\",\"Points\":5}]";

    check("incomplete entries dropped", ra_parse_recent(partial, &list) &&
                                        list.count == 1);
    check("surviving entry is the complete one",
          strcmp(list.items[0].title, "Fine") == 0);

    check("empty array is valid but yields nothing",
          ra_parse_recent("[]", &list) && list.count == 0);

    /* An error payload is an object, not an array. */
    check("object response rejected",
          !ra_parse_recent("{\"Error\":\"bad key\"}", &list));
    check("malformed json rejected", !ra_parse_recent("not json", &list));
    check("null input is safe", !ra_parse_recent(NULL, &list));

    char url[256];
    ra_badge_url("12345", url, sizeof(url));
    check("badge url built",
          strcmp(url, "https://media.retroachievements.org/Badge/12345.png") == 0);

    char path[256];
    ra_badge_path("12345", "sdmc:/badges", path, sizeof(path));
    check("badge cache path built",
          strcmp(path, "sdmc:/badges/12345.png") == 0);

    /* A response longer than the cap must not overrun the fixed array. */
    char *many = malloc(RA_MAX_TROPHIES * 128 + 256);
    if (many) {
        strcpy(many, "[");
        for (int i = 0; i < RA_MAX_TROPHIES + 8; i++) {
            char item[128];
            snprintf(item, sizeof(item),
                     "%s{\"Title\":\"T%d\",\"BadgeName\":\"B%d\",\"Points\":1}",
                     i ? "," : "", i, i);
            strcat(many, item);
        }
        strcat(many, "]");

        check("oversized response clamped to the cap",
              ra_parse_recent(many, &list) && list.count == RA_MAX_TROPHIES);
        free(many);
    }

    printf("\n%s\n", failures ? "FAILURES PRESENT" : "all checks passed");
    return failures != 0;
}
