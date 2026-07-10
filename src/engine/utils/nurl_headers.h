#ifndef NURL_HEADERS_H
#define NURL_HEADERS_H

#include "errors/nurl_error.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char   **keys;       /* canonical-case key strings, heap-owned */
    char   **values;     /* value strings, heap-owned */
    size_t   count;
    size_t   capacity;
} NutHeaderMap;

NutHeaderMap *nurl_headermap_new(void);
nurl_err_t     nurl_headermap_set(NutHeaderMap *m, const char *key, const char *value);
nurl_err_t     nurl_headermap_append(NutHeaderMap *m, const char *key, const char *value);
nurl_err_t     nurl_headermap_add_raw(NutHeaderMap *m, const char *line);
bool           nurl_headermap_has(const NutHeaderMap *m, const char *key);
char          *nurl_headermap_serialize(const NutHeaderMap *m);
void           nurl_headermap_free(NutHeaderMap *m);

#endif /* NURL_HEADERS_H */
