#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Thin HTTPS layer over libcurl.
 *
 * devkitPro builds curl with --with-libnx and the libnx SSL backend, so
 * certificate validation is handled by the console's own ssl service. No CA
 * bundle needs shipping.
 */

bool net_init(void);
void net_exit(void);
bool net_ready(void);

/// GET into a NUL-terminated heap buffer the caller frees. `bearer` may be NULL.
bool net_get(const char *url, const char *bearer, char **out, size_t *out_len);

/// GET straight to a file, writing only once the transfer succeeds.
bool net_download(const char *url, const char *path);

/// Percent-encodes for use in a path segment. Returns `out`.
char *net_urlencode(const char *text, char *out, size_t out_size);
