#ifndef NPS_COOKIES_H
#define NPS_COOKIES_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *domain;
    bool include_subdomains;
    char *path;
    bool secure;
    unsigned long expiry;
    char *name;
    char *value;
} nps_cookie_t;

typedef struct {
    nps_cookie_t *cookies;
    size_t count;
    size_t capacity;
} nps_cookie_jar_t;

/**
 * Creates an empty cookie jar.
 */
nps_cookie_jar_t *nps_cookie_jar_create(void);

/**
 * Loads cookies from a Netscape HTTP Cookie format file.
 * Returns a new cookie jar on success, or NULL if file doesn't exist or is invalid.
 */
nps_cookie_jar_t *nps_cookie_jar_load(const char *filepath);

/**
 * Saves a cookie jar to a file in Netscape HTTP Cookie format.
 * Returns 0 on success, non-zero on error.
 */
int nps_cookie_jar_save(const nps_cookie_jar_t *jar, const char *filepath);

/**
 * Adds or updates a cookie in the jar. If matching domain and name exists, replaces it.
 */
void nps_cookie_jar_add(nps_cookie_jar_t *jar, const nps_cookie_t *cookie);

/**
 * Frees all cookie jar allocations.
 */
void nps_cookie_jar_free(nps_cookie_jar_t *jar);

/**
 * Parses a Set-Cookie header value and adds the resulting cookie to the jar.
 */
void nps_cookie_parse_set_cookie(nps_cookie_jar_t *jar, const char *val, const char *host);

#endif /* NPS_COOKIES_H */
