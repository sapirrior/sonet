#ifndef NPS_DISPATCH_H
#define NPS_DISPATCH_H

#include "nps.h"
#include "engine/nps_ctx.h"

int nps_dispatch(NpsCtx *ctx, const char *method, const char *url, const CommonArgs *args);

#endif /* NPS_DISPATCH_H */
