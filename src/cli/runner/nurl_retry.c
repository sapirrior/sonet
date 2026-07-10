#include "nurl_retry.h"
#include "compat/nurl_compat.h"
#include "utils/nurl_utils.h"
#include "errors/nurl_diag.h"
#include <stdio.h>
#include <stdlib.h>

int execute_with_retry(NutCtx *ctx, NutRequest *req, const CommonArgs *common, nurl_http_response_t **out_res, char **out_effective_url, NutOperationStats *stats) {
    unsigned int max_retries = common->retry;
    unsigned long delay_sec = common->retry_delay > 0 ? common->retry_delay : 1;
    int engine_err = NURL_OK;

    for (unsigned int attempt = 0; attempt <= max_retries; attempt++) {
        if (*out_res) { nurl_http_response_free(*out_res); *out_res = NULL; }
        if (*out_effective_url) { free(*out_effective_url); *out_effective_url = NULL; }

        engine_err = nurl_engine_execute_request(ctx, req, out_res, out_effective_url, stats);

        bool should_retry = false;
        if (engine_err == NURL_OK && *out_res) {
            should_retry = ((*out_res)->status_code >= 500);
            if (should_retry && attempt < max_retries && !common->silent) {
                nurl_diag_warn("HTTP %d. Retrying...", (*out_res)->status_code);
            }
        } else {
            should_retry = (engine_err == NURL_ERR_TIMEOUT || engine_err == NURL_ERR_NETWORK);
            if (should_retry && attempt < max_retries && !common->silent) {
                nurl_diag_warn("Request failed (error %d). Retrying...", engine_err);
            }
        }

        if (!should_retry) break;
        if (attempt < max_retries) nurl_sleep_ms(delay_sec * 1000);
    }
    return engine_err;
}
