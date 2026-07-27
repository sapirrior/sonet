#include "nps_ctx.h"
#include <stdlib.h>

NpsCtx *nps_ctx_create(void) {
    NpsCtx *ctx = calloc(1, sizeof(NpsCtx));
    if (!ctx) return NULL;
    
    ctx->pool = nps_pool_create();
    if (!ctx->pool) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void nps_ctx_destroy(NpsCtx *ctx) {
    if (!ctx) return;
    if (ctx->pool) {
        nps_pool_destroy(ctx->pool);
    }
    free(ctx);
}
