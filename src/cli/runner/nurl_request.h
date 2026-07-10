#ifndef NURL_REQUEST_H
#define NURL_REQUEST_H

#include "nurl.h"
#include "engine/nurl_ctx.h"

/**
 * Performs a generic, secure HTTP/1.1 request (GET, POST, etc.) including redirections,
 * and outputs the response.
 */
int nurl_request_generic(NutCtx *ctx, const char *method, const char *url, const CommonArgs *common);
void dump_headers_to_file(const char *filename, const nurl_http_response_t *res);

#endif /* NURL_REQUEST_H */
