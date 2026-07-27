#ifndef NPS_RETRY_H
#define NPS_RETRY_H

#include "nps.h"
#include "engine/nps_engine_request.h"
#include "nps_engine.h"

/**
 * Executes a request with automatic retries on transient errors.
 */
int execute_with_retry(NpsCtx *ctx, NpsRequest *req, nps_http_response_t **out_res, char **out_effective_url, NpsOperationStats *stats);

#endif /* NPS_RETRY_H */
