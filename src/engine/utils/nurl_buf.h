#ifndef NURL_BUF_H
#define NURL_BUF_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} NutBuf;

void  nurl_buf_init(NutBuf *b);
bool  nurl_buf_append(NutBuf *b, const char *s, size_t n);
bool  nurl_buf_printf(NutBuf *b, const char *fmt, ...);
void  nurl_buf_free(NutBuf *b);
char *nurl_buf_take(NutBuf *b);

#endif /* NURL_BUF_H */
