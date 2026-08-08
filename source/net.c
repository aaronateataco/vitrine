#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <switch.h>

#include "diag.h"
#include "net.h"

#define NET_TIMEOUT_SECONDS 20
#define NET_MAX_BODY        (8 * 1024 * 1024)

static bool g_ready = false;

bool net_init(void)
{
    if (g_ready)
        return true;

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        diag_logf("socketInitializeDefault failed: 0x%x", rc);
        return false;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        diag_logf("curl_global_init failed");
        socketExit();
        return false;
    }

    g_ready = true;
    diag_logf("network ready (%s)", curl_version());
    return true;
}

void net_exit(void)
{
    if (!g_ready)
        return;

    curl_global_cleanup();
    socketExit();
    g_ready = false;
}

bool net_ready(void)
{
    return g_ready;
}

typedef struct {
    char  *data;
    size_t len;
    size_t capacity;
} Buffer;

static size_t on_data(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    Buffer *buffer = userdata;
    size_t chunk = size * nmemb;

    if (buffer->len + chunk + 1 > NET_MAX_BODY)
        return 0;   /* Aborts the transfer. */

    if (buffer->len + chunk + 1 > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 8192;
        while (capacity < buffer->len + chunk + 1)
            capacity *= 2;

        char *grown = realloc(buffer->data, capacity);
        if (!grown)
            return 0;

        buffer->data = grown;
        buffer->capacity = capacity;
    }

    memcpy(buffer->data + buffer->len, ptr, chunk);
    buffer->len += chunk;
    buffer->data[buffer->len] = '\0';
    return chunk;
}

static CURL *make_handle(const char *url, const char *bearer,
                         struct curl_slist **out_headers)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)NET_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "vitrine-nx/0.1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (bearer && bearer[0]) {
        char header[256];
        snprintf(header, sizeof(header), "Authorization: Bearer %s", bearer);
        *out_headers = curl_slist_append(NULL, header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *out_headers);
    }

    return curl;
}

bool net_get(const char *url, const char *bearer, char **out, size_t *out_len)
{
    if (!g_ready || !out)
        return false;

    *out = NULL;
    if (out_len)
        *out_len = 0;

    struct curl_slist *headers = NULL;
    CURL *curl = make_handle(url, bearer, &headers);
    if (!curl)
        return false;

    Buffer buffer = { NULL, 0, 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (code != CURLE_OK || status < 200 || status >= 300) {
        diag_logf("GET %s -> curl %d, http %ld", url, code, status);
        free(buffer.data);
        return false;
    }

    *out = buffer.data;
    if (out_len)
        *out_len = buffer.len;
    return true;
}

bool net_download(const char *url, const char *path)
{
    char *body = NULL;
    size_t len = 0;

    if (!net_get(url, NULL, &body, &len) || len == 0) {
        free(body);
        return false;
    }

    /* Write to a temporary first, so an interrupted transfer cannot leave a
       truncated file that later looks like a valid cached cover. */
    char temp[512];
    snprintf(temp, sizeof(temp), "%s.part", path);

    FILE *file = fopen(temp, "wb");
    if (!file) {
        free(body);
        return false;
    }

    bool ok = fwrite(body, 1, len, file) == len;
    fclose(file);
    free(body);

    if (!ok) {
        remove(temp);
        return false;
    }

    remove(path);
    if (rename(temp, path) != 0) {
        remove(temp);
        return false;
    }

    return true;
}

char *net_urlencode(const char *text, char *out, size_t out_size)
{
    static const char *hex = "0123456789ABCDEF";
    size_t written = 0;

    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        bool safe = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
                    *p == '.' || *p == '~';

        if (safe) {
            if (written + 2 > out_size)
                break;
            out[written++] = (char)*p;
        } else {
            if (written + 4 > out_size)
                break;
            out[written++] = '%';
            out[written++] = hex[*p >> 4];
            out[written++] = hex[*p & 0x0f];
        }
    }

    out[written] = '\0';
    return out;
}
