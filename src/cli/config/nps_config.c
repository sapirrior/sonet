#include "nps_config.h"
#include "nps_utils.h"
#include "errors/nps_diag.h"
#include "compat/nps_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <errno.h>

static void strip_quotes(char *str) {
    size_t len = strlen(str);
    if (len >= 2 && ((str[0] == '"' && str[len - 1] == '"') || (str[0] == '\'' && str[len - 1] == '\''))) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

static bool has_header(char **headers, size_t count, const char *key) {
    size_t key_len = strlen(key);
    for (size_t i = 0; i < count; i++) {
        if (nps_strncasecmp(headers[i], key, key_len) == 0 && headers[i][key_len] == ':') {
            return true;
        }
    }
    return false;
}

void nps_config_load_and_merge(CommonArgs *args) {
    char *config_path = getenv("NPS_CONFIG");
    char *allocated_path = NULL;
    bool explicitly_set = (config_path != NULL);

    if (!config_path) {
        char *home = getenv("HOME");
        if (home) {
            size_t needed = strlen(home) + 32;
            allocated_path = malloc(needed);
            if (allocated_path) {
                snprintf(allocated_path, needed, "%s/.config/nps/config.toml", home);
                config_path = allocated_path;
            }
        }
    }

    if (!config_path) return;

    FILE *f = fopen(config_path, "r");
    if (!f) {
        // Only warn if explicitly set via env var
        if (explicitly_set) {
            nps_diag_warn("could not open config file '%s': %s", config_path, strerror(errno));
        }
        free(allocated_path);
        return;
    }
    free(allocated_path);

    char line[1024];
    int section = 0; // 0 = none, 1 = defaults, 2 = headers

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = nps_utils_trim(line);
        if (strlen(trimmed) == 0 || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        if (trimmed[0] == '[') {
            if (nps_strcasecmp(trimmed, "[defaults]") == 0) {
                section = 1;
            } else if (nps_strcasecmp(trimmed, "[headers]") == 0) {
                section = 2;
            } else {
                section = 0;
            }
            continue;
        }

        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = nps_utils_trim(trimmed);
            char *val = nps_utils_trim(eq + 1);
            strip_quotes(val);

            if (section == 1) {
                // [defaults]
                if (nps_strcasecmp(key, "timeout") == 0 && !args->is_set.timeout) {
                    args->timeout = strtoul(val, NULL, 10);
                } else if (nps_strcasecmp(key, "connect_timeout") == 0 && !args->is_set.connect_timeout) {
                    args->connect_timeout = strtoul(val, NULL, 10);
                } else if (nps_strcasecmp(key, "follow_redirects") == 0 && !args->is_set.location) {
                    if (nps_strcasecmp(val, "true") == 0) {
                        args->location = true;
                    }
                } else if (nps_strcasecmp(key, "user_agent") == 0 && !args->is_set.user_agent) {
                    if (args->user_agent) free(args->user_agent);
                    args->user_agent = strdup(val);
                }
            } else if (section == 2) {
                // [headers]
                if (!has_header(args->header, args->header_count, key)) {
                    size_t needed = strlen(key) + strlen(val) + 4;
                    char *hdr_line = malloc(needed);
                    if (hdr_line) {
                        snprintf(hdr_line, needed, "%s: %s", key, val);
                        char **temp = realloc(args->header, sizeof(char *) * (args->header_count + 1));
                        if (temp) {
                            args->header = temp;
                            args->header[args->header_count] = hdr_line;
                            args->header_count++;
                        } else {
                            free(hdr_line);
                        }
                    }
                }
            }
        }
    }

    fclose(f);

    // Environment variables
    if (!args->proxy) {
        char *p = getenv("https_proxy");
        if (!p) p = getenv("HTTPS_PROXY");
        if (!p) p = getenv("http_proxy");
        if (!p) p = getenv("HTTP_PROXY");
        if (p) args->proxy = strdup(p);
    }
    if (!args->cacert) {
        char *c = getenv("NPS_CACERT");
        if (c) args->cacert = strdup(c);
    }
}
