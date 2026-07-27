#include "nps_dispatch.h"
#include "nps_request.h"
#include "engine/nps_engine.h"
#include "engine/nps_engine_request.h"
#include "nps_retry.h"
#include "compat/nps_compat.h"
#include "errors/nps_diag.h"
#include "errors/nps_error_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int nps_mode_upload(NpsCtx *ctx, const char *url, const CommonArgs *common) {
    if (!common->upload_file) { nps_diag_err("no upload file specified."); return NPS_ERR_ARG; }
    struct stat st;
    if (nps_stat(common->upload_file, &st) != 0 || !NPS_S_ISREG(st.st_mode)) { nps_diag_err("could not read upload file '%s'.", common->upload_file); return NPS_ERR_IO; }

    nps_http_response_t *res = NULL; char *eff_url = NULL; NpsOperationStats stats = {0};
    NpsRequest *req = nps_request_new();
    nps_request_from_args(req, common->method ? common->method : "POST", url, common);

    int err = execute_with_retry(ctx, req, &res, &eff_url, &stats);
    if (res && common->dump_header) dump_headers_to_file(common->dump_header, res);
    if (err != NPS_OK && !common->silent) nps_handle_request_error(err, req, eff_url ? eff_url : url);
    else if (res && !common->silent && res->status_code >= 400) nps_handle_request_error(res->status_code >= 500 ? NPS_ERR_HTTP_5XX : NPS_ERR_HTTP_4XX, req, eff_url ? eff_url : url);

    bool should_suppress = (common->fail && res && res->status_code >= 400 && !common->fail_with_body);
    if (err == NPS_OK && res && !common->silent && !should_suppress) {
        if (res->body_len > 0) fwrite(res->body, 1, res->body_len, stdout);
    }
    int ret = (err != NPS_OK) ? err : (res && res->status_code >= 400 ? (res->status_code >= 500 ? NPS_ERR_HTTP_5XX : NPS_ERR_HTTP_4XX) : NPS_OK);
    nps_request_free(req); if (res) nps_http_response_free(res); free(eff_url);
    return ret;
}
