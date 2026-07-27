#ifndef NPS_ERROR_HANDLER_H
#define NPS_ERROR_HANDLER_H

#include "nps_error.h"
#include "engine/nps_engine_request.h"

/**
 * Centrally handles request errors by emitting smart, context-aware diagnostics.
 */
void nps_handle_request_error(nps_err_t err, const NpsRequest *req, const char *target_url);

#endif /* NPS_ERROR_HANDLER_H */
