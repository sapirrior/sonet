#include "nps_dispatch.h"
#include "nps_request.h"

int nps_dispatch(NpsCtx *ctx, const char *method, const char *url, const CommonArgs *args) {
    if (args->dry_run)     return nps_mode_inspect(url, args);
    if (args->ping)        return nps_mode_ping(url, args);
    if (args->resolve)     return nps_mode_resolve(url, args);
    if (args->download)    return nps_mode_download(ctx, url, args);
    if (args->upload_file) return nps_mode_upload(ctx, url, args);
    return nps_request_generic(ctx, method, url, args);
}
