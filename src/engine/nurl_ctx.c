#include "nurl_ctx.h"
#include <stdlib.h>

NutCtx *nurl_ctx_create(void) {
    NutCtx *ctx = calloc(1, sizeof(NutCtx));
    if (!ctx) return NULL;
    
    ctx->pool = nurl_pool_create();
    if (!ctx->pool) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void nurl_ctx_destroy(NutCtx *ctx) {
    if (!ctx) return;
    if (ctx->pool) {
        nurl_pool_destroy(ctx->pool);
    }
    free(ctx);
}
