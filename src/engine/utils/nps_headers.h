#ifndef NPS_HEADERS_H
#define NPS_HEADERS_H

#include "errors/nps_error.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char   **keys;       /* canonical-case key strings, heap-owned */
    char   **values;     /* value strings, heap-owned */
    size_t   count;
    size_t   capacity;
} NpsHeaderMap;

NpsHeaderMap *nps_headermap_new(void);
nps_err_t     nps_headermap_set(NpsHeaderMap *m, const char *key, const char *value);
nps_err_t     nps_headermap_append(NpsHeaderMap *m, const char *key, const char *value);
nps_err_t     nps_headermap_add_raw(NpsHeaderMap *m, const char *line);
bool           nps_headermap_has(const NpsHeaderMap *m, const char *key);
char          *nps_headermap_serialize(const NpsHeaderMap *m);
void           nps_headermap_free(NpsHeaderMap *m);

#endif /* NPS_HEADERS_H */
