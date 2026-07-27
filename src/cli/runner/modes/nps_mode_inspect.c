#include "nps_dispatch.h"
#include "utils/nps_utils.h"
#include "compat/nps_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int nps_mode_inspect(const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL;
    int port = 0;
    if (nps_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NPS_ERR_URL;

    printf("> %s %s HTTP/1.1\n", common->method ? common->method : "GET", path);
    printf("> Host: %s\n", host);
    printf("> User-Agent: nps/" NPS_VERSION "\n");
    printf("> Connection: close\n");

    for (size_t i = 0; i < common->header_count; i++) {
        char *line = strdup(common->header[i]);
        if (line) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *key = line, *val = colon + 1;
                while (*val && isspace((unsigned char)*val)) val++;
                printf("> %s: %s\n", key, nps_utils_redact_header(key, val));
            } else printf("> %s\n", common->header[i]);
            free(line);
        }
    }

    if (!common->no_auth) {
        bool has_auth = false;
        for (size_t i = 0; i < common->header_count; i++) {
            if (nps_strncasecmp(common->header[i], "Authorization:", 14) == 0) { has_auth = true; break; }
        }
        if ((common->bearer || common->token || common->user) && !has_auth) printf("> Authorization: [hidden]\n");
    }

    bool has_ct = false;
    for (size_t i = 0; i < common->header_count; i++) {
        if (nps_strncasecmp(common->header[i], "Content-Type:", 13) == 0) { has_ct = true; break; }
    }
    if (common->json && !has_ct) printf("> Content-Type: application/json\n");

    size_t body_len = (common->data) ? (common->data_len > 0 ? common->data_len : strlen(common->data)) : 0;
    bool has_cl = false;
    for (size_t i = 0; i < common->header_count; i++) {
        if (nps_strncasecmp(common->header[i], "Content-Length:", 15) == 0) { has_cl = true; break; }
    }
    if (body_len > 0 && !has_cl) printf("> Content-Length: %zu\n", body_len);

    printf(">\n");
    if (common->data && body_len > 0) {
        fwrite(common->data, 1, body_len, stdout);
        printf("\n");
    }

    free(scheme); free(host); free(path);
    return 0;
}
