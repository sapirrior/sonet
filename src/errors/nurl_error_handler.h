#ifndef NURL_ERROR_HANDLER_H
#define NURL_ERROR_HANDLER_H

#include "nurl_error.h"
#include "engine/nurl_engine_request.h"

/**
 * Centrally handles request errors by emitting smart, context-aware diagnostics.
 */
void nurl_handle_request_error(nurl_err_t err, const NutRequest *req, const char *target_url);

#endif /* NURL_ERROR_HANDLER_H */
