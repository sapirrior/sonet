#include "nurl_buf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void nurl_buf_init(NutBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

bool nurl_buf_grow(NutBuf *b, size_t needed) {
    if (b->len + needed >= b->cap) {
        size_t new_cap = b->cap == 0 ? 128 : b->cap * 2;
        while (b->len + needed >= new_cap) new_cap *= 2;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return false;
        b->data = new_data;
        b->cap = new_cap;
    }
    return true;
}

bool nurl_buf_append(NutBuf *b, const char *s, size_t n) {
    if (!nurl_buf_grow(b, n + 1)) return false;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

bool nurl_buf_printf(NutBuf *b, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return false;
    }

    if (!nurl_buf_grow(b, (size_t)needed + 1)) {
        va_end(args);
        return false;
    }

    vsnprintf(b->data + b->len, b->cap - b->len, fmt, args);
    b->len += (size_t)needed;
    va_end(args);
    return true;
}

void nurl_buf_free(NutBuf *b) {
    if (b->data) free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

char *nurl_buf_take(NutBuf *b) {
    char *res = b->data;
    if (!res) res = strdup("");
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return res;
}
