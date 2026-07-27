#include "nps_redirect.h"
#include "nps_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *nps_resolve_redirect(const char *current_url, const char *location) {
    if (strstr(location, "://")) {
        return strdup(location);
    }

    char *scheme = NULL;
    char *host = NULL;
    char *path = NULL;
    int port = 0;

    if (nps_utils_parse_url(current_url, &scheme, &host, &port, &path) != 0) {
        return strdup(location);
    }

    char *resolved = NULL;
    int ret = -1;
    if (location[0] == '/') {
        if (port == 80 || port == 443) {
            ret = asprintf(&resolved, "%s://%s%s", scheme, host, location);
        } else {
            ret = asprintf(&resolved, "%s://%s:%d%s", scheme, host, port, location);
        }
    } else {
        char *last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
        }
        if (port == 80 || port == 443) {
            ret = asprintf(&resolved, "%s://%s%s/%s", scheme, host, path, location);
        } else {
            ret = asprintf(&resolved, "%s://%s:%d%s/%s", scheme, host, port, path, location);
        }
    }

    if (ret < 0) {
        resolved = NULL;
    }

    free(scheme);
    free(host);
    free(path);

    return resolved;
}
