#include "nps_cookies.h"
#include "errors/nps_diag.h"
#include "compat/nps_compat.h"
#include "nps_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

nps_cookie_jar_t *nps_cookie_jar_create(void) {
    nps_cookie_jar_t *jar = calloc(1, sizeof(nps_cookie_jar_t));
    return jar;
}

void nps_cookie_jar_free(nps_cookie_jar_t *jar) {
    if (!jar) return;
    for (size_t i = 0; i < jar->count; i++) {
        free(jar->cookies[i].domain);
        free(jar->cookies[i].path);
        free(jar->cookies[i].name);
        free(jar->cookies[i].value);
    }
    free(jar->cookies);
    free(jar);
}

nps_cookie_jar_t *nps_cookie_jar_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) {
            nps_diag_err("could not open cookie jar '%s' for reading: %s", path, strerror(errno));
        }
        return NULL;
    }

    nps_cookie_jar_t *jar = nps_cookie_jar_create();
    if (!jar) { fclose(f); return NULL; }

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char *parts[7];
        int part_count = 0;
        char *tok = strtok(line, "\t\n\r");
        while (tok && part_count < 7) {
            parts[part_count++] = tok;
            tok = strtok(NULL, "\t\n\r");
        }

        if (part_count >= 7) {
            nps_cookie_t c;
            c.domain = strdup(parts[0]);
            c.include_subdomains = (nps_strcasecmp(parts[1], "TRUE") == 0);
            c.path = strdup(parts[2]);
            c.secure = (nps_strcasecmp(parts[3], "TRUE") == 0);
            c.expiry = strtoul(parts[4], NULL, 10);
            c.name = strdup(parts[5]);
            c.value = strdup(parts[6]);

            if (c.domain && c.path && c.name && c.value) {
                nps_cookie_jar_add(jar, &c);
            }
            free(c.domain); free(c.path); free(c.name); free(c.value);
        }
    }

    fclose(f);
    return jar;
}

int nps_cookie_jar_save(const nps_cookie_jar_t *jar, const char *path) {
    if (!jar) return -1;
    FILE *f = fopen(path, "w");
    if (!f) {
        nps_diag_err("could not open cookie jar '%s' for writing: %s", path, strerror(errno));
        return -1;
    }

    fprintf(f, "# Netscape HTTP Cookie File\n");
    for (size_t i = 0; i < jar->count; i++) {
        nps_cookie_t *c = &jar->cookies[i];
        fprintf(f, "%s\t%s\t%s\t%s\t%lu\t%s\t%s\n",
            c->domain,
            c->include_subdomains ? "TRUE" : "FALSE",
            c->path,
            c->secure ? "TRUE" : "FALSE",
            (unsigned long)c->expiry,
            c->name,
            c->value);
    }
    fclose(f);
    return 0;
}

void nps_cookie_jar_add(nps_cookie_jar_t *jar, const nps_cookie_t *cookie) {
    if (!jar || !cookie) return;

    // Check for existing cookie (same domain, path, name)
    for (size_t i = 0; i < jar->count; i++) {
        if (strcmp(jar->cookies[i].domain, cookie->domain) == 0 &&
            strcmp(jar->cookies[i].path, cookie->path) == 0 &&
            strcmp(jar->cookies[i].name, cookie->name) == 0) {
            
            free(jar->cookies[i].value);
            jar->cookies[i].value = strdup(cookie->value);
            jar->cookies[i].expiry = cookie->expiry;
            jar->cookies[i].secure = cookie->secure;
            return;
        }
    }

    nps_cookie_t *temp = realloc(jar->cookies, sizeof(nps_cookie_t) * (jar->count + 1));
    if (temp) {
        jar->cookies = temp;
        jar->cookies[jar->count].domain = strdup(cookie->domain);
        jar->cookies[jar->count].path = strdup(cookie->path);
        jar->cookies[jar->count].name = strdup(cookie->name);
        jar->cookies[jar->count].value = strdup(cookie->value);
        jar->cookies[jar->count].include_subdomains = cookie->include_subdomains;
        jar->cookies[jar->count].secure = cookie->secure;
        jar->cookies[jar->count].expiry = cookie->expiry;
        jar->count++;
    }
}

void nps_cookie_parse_set_cookie(nps_cookie_jar_t *jar, const char *val, const char *host) {
    if (!jar || !val || !host) return;

    char *val_dup = strdup(val);
    if (!val_dup) return;

    char *parts[32];
    size_t part_count = 0;
    char *tok = val_dup;
    while (part_count < 32) {
        char *semi = strchr(tok, ';');
        if (semi) {
            *semi = '\0';
            parts[part_count++] = tok;
            tok = semi + 1;
        } else {
            parts[part_count++] = tok;
            break;
        }
    }

    if (part_count > 0) {
        char *eq = strchr(parts[0], '=');
        if (eq) {
            *eq = '\0';
            char *name = nps_utils_trim(parts[0]);
            char *value = nps_utils_trim(eq + 1);
            char *domain = NULL;
            char *cookie_path = NULL;
            bool secure = false;

            for (size_t p = 1; p < part_count; p++) {
                char *attr = parts[p];
                char *attr_eq = strchr(attr, '=');
                if (attr_eq) {
                    *attr_eq = '\0';
                    char *k_attr = nps_utils_trim(attr);
                    char *v_attr = nps_utils_trim(attr_eq + 1);
                    if (nps_strcasecmp(k_attr, "domain") == 0) {
                        if (domain) free(domain);
                        domain = strdup(v_attr);
                    } else if (nps_strcasecmp(k_attr, "path") == 0) {
                        if (cookie_path) free(cookie_path);
                        cookie_path = strdup(v_attr);
                    }
                } else {
                    char *k_attr = nps_utils_trim(attr);
                    if (nps_strcasecmp(k_attr, "secure") == 0) {
                        secure = true;
                    }
                }
            }

            if (!domain) {
                domain = strdup(host);
            }
            if (!cookie_path) {
                cookie_path = strdup("/");
            }

            nps_cookie_t c;
            c.domain = domain;
            c.include_subdomains = true;
            c.path = cookie_path;
            c.secure = secure;
            c.expiry = 0;
            c.name = name;
            c.value = value;

            nps_cookie_jar_add(jar, &c);

            free(domain);
            free(cookie_path);
        }
    }
    free(val_dup);
}
