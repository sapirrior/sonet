#ifndef NURL_DISPATCH_H
#define NURL_DISPATCH_H

#include "nurl.h"
#include "engine/nurl_ctx.h"

int nurl_dispatch(NutCtx *ctx, const char *method, const char *url, const CommonArgs *args);

#endif /* NURL_DISPATCH_H */
