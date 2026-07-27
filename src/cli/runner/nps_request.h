#ifndef NPS_REQUEST_H
#define NPS_REQUEST_H

#include "nps.h"
#include "engine/nps_ctx.h"

/**
 * Performs a generic, secure HTTP/1.1 request (GET, POST, etc.) including redirections,
 * and outputs the response.
 */
int nps_request_generic(NpsCtx *ctx, const char *method, const char *url, const CommonArgs *common);
void dump_headers_to_file(const char *filename, const nps_http_response_t *res);

#endif /* NPS_REQUEST_H */
