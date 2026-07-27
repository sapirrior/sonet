#ifndef NPS_BUF_H
#define NPS_BUF_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} NpsBuf;

void  nps_buf_init(NpsBuf *b);
bool  nps_buf_append(NpsBuf *b, const char *s, size_t n);
bool  nps_buf_printf(NpsBuf *b, const char *fmt, ...);
void  nps_buf_free(NpsBuf *b);
char *nps_buf_take(NpsBuf *b);

#endif /* NPS_BUF_H */
