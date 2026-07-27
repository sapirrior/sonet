#ifndef NPS_DISPATCH_H
#define NPS_DISPATCH_H

#include "nps.h"
#include "engine/nps_ctx.h"

int nps_dispatch(NpsCtx *ctx, const char *method, const char *url, const CommonArgs *args);

int nps_mode_inspect(const char *url, const CommonArgs *common);
int nps_mode_resolve(const char *url_or_host, const CommonArgs *common);
int nps_mode_ping(const char *url, const CommonArgs *common);
int nps_mode_download(NpsCtx *ctx, const char *url, const CommonArgs *common);
int nps_mode_upload(NpsCtx *ctx, const char *url, const CommonArgs *common);

#endif /* NPS_DISPATCH_H */
