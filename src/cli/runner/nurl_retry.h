#ifndef NURL_RETRY_H
#define NURL_RETRY_H

#include "nurl.h"
#include "engine/nurl_engine_request.h"
#include "nurl_engine.h"

/**
 * Executes a request with automatic retries on transient errors.
 */
int execute_with_retry(NutCtx *ctx, NutRequest *req, const CommonArgs *common, nurl_http_response_t **out_res, char **out_effective_url, NutOperationStats *stats);

#endif /* NURL_RETRY_H */
