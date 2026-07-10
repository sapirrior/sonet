#ifndef NURL_CTX_H
#define NURL_CTX_H

#include "nurl_pool.h"

typedef struct {
    NutConnPool *pool;
} NutCtx;

NutCtx *nurl_ctx_create(void);
void     nurl_ctx_destroy(NutCtx *ctx);

#endif /* NURL_CTX_H */
