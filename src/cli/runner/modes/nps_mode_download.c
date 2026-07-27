#include "nps_dispatch.h"
#include "nps_request.h"
#include "engine/nps_engine.h"
#include "engine/nps_engine_request.h"
#include "nps_retry.h"
#include "net/nps_net.h"
#include "tls/nps_tls.h"
#include "nps_progress.h"
#include "utils/nps_utils.h"
#include "errors/nps_diag.h"
#include "errors/nps_error_handler.h"
#include "compat/nps_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static char *extract_filename_from_cd(const nps_http_response_t *res) {
    for (size_t i = 0; i < res->header_count; i++) {
        if (nps_strncasecmp(res->headers[i], "Content-Disposition:", 20) == 0) {
            char *fn = strdup(res->headers[i] + 20);
            if (fn) {
                char *filename = strstr(fn, "filename=");
                if (filename) {
                    filename += 9;
                    if (filename[0] == '"') {
                        memmove(filename, filename + 1, strlen(filename));
                        char *end = strchr(filename, '"');
                        if (end) *end = '\0';
                    }
                    // Strip any path traversal or invalid chars for safety
                    for (char *p = filename; *p; p++) {
                        if (*p == '/' || *p == '\\') *p = '_';
                    }
                    char *ret = strdup(filename);
                    free(fn);
                    return ret;
                }
                free(fn);
            }
        }
    }
    return NULL;
}

typedef struct {
    char **filename_ptr;
    bool is_stdout;
    unsigned long resume_offset;
    bool silent;
} DownloadCtx;

static void nps_download_header_cb(NpsRequest *req, const nps_http_response_t *res, void *user_data) {
    DownloadCtx *dctx = (DownloadCtx *)user_data;
    if (res->status_code >= 300 && res->status_code < 400) return; // Ignore redirects
    if (req->out) return; // Already open

    char *cd_filename = extract_filename_from_cd(res);
    if (cd_filename) {
        if (!dctx->silent) fprintf(stderr, "* Found filename in Content-Disposition: %s\n", cd_filename);
        free(*dctx->filename_ptr);
        *dctx->filename_ptr = cd_filename;
    }

    if (!dctx->is_stdout) {
        req->out = fopen(*dctx->filename_ptr, (dctx->resume_offset > 0) ? "ab" : "wb");
        if (!req->out) {
            nps_diag_err("could not open '%s' for writing: %s", *dctx->filename_ptr, strerror(errno));
        }
    } else {
        req->out = stdout;
    }
}

int nps_mode_download(NpsCtx *ctx, const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL, *filename = NULL;
    int port = 0;
    if (nps_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NPS_ERR_URL;

    if (common->output) filename = strdup(common->output);
    else {
        char *last_slash = strrchr(path, '/');
        if (last_slash && strlen(last_slash) > 1) {
            char *q = strchr(last_slash + 1, '?'); if (q) *q = '\0';
            filename = strdup(last_slash + 1); if (q) *q = '?';
        } else filename = strdup("download");
    }

    bool is_stdout = (strcmp(filename, "-") == 0);
    unsigned long start_pos = 0;
    if (common->resume && !is_stdout) {
        struct stat st;
        if (nps_stat(filename, &st) == 0 && NPS_S_ISREG(st.st_mode)) start_pos = (unsigned long)st.st_size;
    }

    NpsRequest *req = nps_request_new();
    nps_request_from_args(req, "GET", url, common);
    NpsProgressCtx p_ctx = { .resume_offset = start_pos, .silent = common->silent, .start_time = nps_utils_get_time_sec(), .last_update = 0 };
    if (common->progress) { req->progress_cb = nps_progress_update; req->progress_data = &p_ctx; }

    DownloadCtx dctx = { .filename_ptr = &filename, .is_stdout = is_stdout, .resume_offset = start_pos, .silent = common->silent };
    req->header_cb = nps_download_header_cb;
    req->header_data = &dctx;
    req->resume_offset = start_pos;

    if (!common->silent) {
        if (common->output) fprintf(stderr, "* Saving to: %s\n", filename);
        else fprintf(stderr, "* Saving to: %s (will check Content-Disposition)\n", filename);
        if (start_pos > 0) nps_diag_hint("resuming download from offset: %lu", start_pos);
    }

    nps_http_response_t *res = NULL;
    char *effective_url = NULL;
    NpsOperationStats stats = {0};

    // Note: req->out is opened in the header callback
    int err = execute_with_retry(ctx, req, &res, &effective_url, &stats);
    if (req->out && !is_stdout) fclose(req->out);

    if (res && common->dump_header) dump_headers_to_file(common->dump_header, res);

    if (err != NPS_OK && !common->silent) nps_handle_request_error(err, req, effective_url ? effective_url : url);
    else if (res && !common->silent && res->status_code >= 400) nps_handle_request_error(res->status_code >= 500 ? NPS_ERR_HTTP_5XX : NPS_ERR_HTTP_4XX, req, effective_url ? effective_url : url);

    nps_request_free(req); if (res) nps_http_response_free(res);
    free(effective_url); free(filename); free(scheme); free(host); free(path);
    return err;
}
