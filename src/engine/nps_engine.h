#ifndef NPS_ENGINE_H
#define NPS_ENGINE_H

#include "engine/nps_engine_request.h"
#include "nps_http.h"

#include "nps_ctx.h"

typedef struct {
    double connect_time_sec;
    int    num_redirects;
} NpsOperationStats;

int nps_engine_execute_request(
    NpsCtx *ctx,
    NpsRequest *req,
    nps_http_response_t **out_response,
    char **out_effective_url,
    NpsOperationStats *out_stats
);

#endif /* NPS_ENGINE_H */
