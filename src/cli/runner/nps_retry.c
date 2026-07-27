#include "nps_retry.h"
#include "compat/nps_compat.h"
#include "utils/nps_utils.h"
#include "errors/nps_diag.h"
#include <stdio.h>
#include <stdlib.h>

int execute_with_retry(NpsCtx *ctx, NpsRequest *req, nps_http_response_t **out_res, char **out_effective_url, NpsOperationStats *stats) {
    unsigned int max_retries = req->retry_count;
    unsigned long delay_sec = req->retry_delay_sec > 0 ? req->retry_delay_sec : 1;
    int engine_err = NPS_OK;

    for (unsigned int attempt = 0; attempt <= max_retries; attempt++) {
        if (*out_res) { nps_http_response_free(*out_res); *out_res = NULL; }
        if (*out_effective_url) { free(*out_effective_url); *out_effective_url = NULL; }

        engine_err = nps_engine_execute_request(ctx, req, out_res, out_effective_url, stats);

        bool should_retry = false;
        if (engine_err == NPS_OK && *out_res) {
            should_retry = ((*out_res)->status_code >= 500);
            if (should_retry && attempt < max_retries && !req->silent) {
                nps_diag_warn("HTTP %d. Retrying...", (*out_res)->status_code);
            }
        } else {
            should_retry = (engine_err == NPS_ERR_TIMEOUT || engine_err == NPS_ERR_NETWORK);
            if (should_retry && attempt < max_retries && !req->silent) {
                nps_diag_warn("Request failed (error %d). Retrying...", engine_err);
            }
        }

        if (!should_retry) break;
        if (attempt < max_retries) nps_sleep_ms(delay_sec * 1000);
    }
    return engine_err;
}
