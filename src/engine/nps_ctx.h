#ifndef NPS_CTX_H
#define NPS_CTX_H

#include "nps_pool.h"

typedef struct {
    NpsConnPool *pool;
} NpsCtx;

NpsCtx *nps_ctx_create(void);
void     nps_ctx_destroy(NpsCtx *ctx);

#endif /* NPS_CTX_H */
