#include "nurl_dispatch.h"
#include "nurl_request.h"
#include "nurl_engine.h"
#include "nurl_engine_request.h"
#include "nurl_retry.h"
#include "nurl_net.h"
#include "nurl_tls.h"
#include "nurl_progress.h"
#include "utils/nurl_utils.h"
#include "errors/nurl_diag.h"
#include "errors/nurl_error_handler.h"
#include "compat/nurl_compat.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

static int nurl_mode_inspect(const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL;
    int port = 0;
    if (nurl_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NURL_ERR_URL;

    printf("> %s %s HTTP/1.1\n", common->method ? common->method : "GET", path);
    printf("> Host: %s\n", host);
    printf("> User-Agent: nurl/" NURL_VERSION "\n");
    printf("> Connection: close\n");

    for (size_t i = 0; i < common->header_count; i++) {
        char *line = strdup(common->header[i]);
        if (line) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *key = line, *val = colon + 1;
                while (*val && isspace((unsigned char)*val)) val++;
                printf("> %s: %s\n", key, nurl_utils_redact_header(key, val));
            } else printf("> %s\n", common->header[i]);
            free(line);
        }
    }

    if (!common->no_auth) {
        bool has_auth = false;
        for (size_t i = 0; i < common->header_count; i++) {
            if (nurl_strncasecmp(common->header[i], "Authorization:", 14) == 0) { has_auth = true; break; }
        }
        if ((common->bearer || common->token || common->user) && !has_auth) printf("> Authorization: [hidden]\n");
    }

    bool has_ct = false;
    for (size_t i = 0; i < common->header_count; i++) {
        if (nurl_strncasecmp(common->header[i], "Content-Type:", 13) == 0) { has_ct = true; break; }
    }
    if (common->json && !has_ct) printf("> Content-Type: application/json\n");

    size_t body_len = (common->data) ? (common->data_len > 0 ? common->data_len : strlen(common->data)) : 0;
    bool has_cl = false;
    for (size_t i = 0; i < common->header_count; i++) {
        if (nurl_strncasecmp(common->header[i], "Content-Length:", 15) == 0) { has_cl = true; break; }
    }
    if (body_len > 0 && !has_cl) printf("> Content-Length: %zu\n", body_len);

    printf(">\n");
    if (common->data && body_len > 0) {
        fwrite(common->data, 1, body_len, stdout);
        printf("\n");
    }

    free(scheme); free(host); free(path);
    return 0;
}

static int nurl_mode_resolve(const char *url_or_host, const CommonArgs *common) {
    (void)common;
    char *scheme = NULL, *host = NULL, *path = NULL, *target_host = NULL;
    int port = 0;
    if (strstr(url_or_host, "://")) {
        if (nurl_utils_parse_url(url_or_host, &scheme, &host, &port, &path) == 0) target_host = strdup(host);
    }
    if (!target_host) target_host = strdup(url_or_host);
    if (!target_host) { free(scheme); free(host); free(path); return 2; }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(target_host, NULL, &hints, &result) != 0) {
        nurl_diag_err("DNS resolution failed for '%s'.", target_host);
        nurl_diag_hint("check your internet connection or verify the hostname is correct.");
        free(target_host); free(scheme); free(host); free(path); return 2;
    }

    bool found = false;
    char ip_str[INET6_ADDRSTRLEN];
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        found = true;
        void *addr;
        const char *record_type;
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr = &(ipv4->sin_addr); record_type = "A";
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr = &(ipv6->sin6_addr); record_type = "AAAA";
        }
        inet_ntop(rp->ai_family, addr, ip_str, sizeof(ip_str));
        if (!common->silent) printf("%s\t%s\t%s\n", target_host, ip_str, record_type);
    }
    freeaddrinfo(result); free(target_host); free(scheme); free(host); free(path);
    return found ? 0 : 2;
}

static int nurl_mode_ping(const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL;
    int port = 0;
    if (nurl_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NURL_ERR_URL;

    unsigned int count = common->ping_count > 0 ? common->ping_count : 1;
    unsigned long interval = common->ping_interval > 0 ? common->ping_interval : 1000;

    nurl_err_t err = NURL_OK;
    int fd = nurl_net_connect_proxy_ex(host, port, common->proxy, common->proxy_user, common->no_proxy, common->connect_to, (unsigned int)common->connect_timeout, &err);
    if (fd < 0) { free(scheme); free(host); free(path); return NURL_ERR_CONNECT; }

    bool use_tls = (nurl_strcasecmp(scheme, "https") == 0);
    nurl_tls_t *t = NULL;
    if (use_tls) {
        t = nurl_tls_create(!common->no_verify, common->cacert, common->cert, common->key, common->tls12, common->tls13);
        if (!t) { nurl_net_close(fd); free(scheme); free(host); free(path); return NURL_ERR_TLS; }
        if (nurl_tls_handshake(t, fd, host) != 0) { nurl_tls_free(t); nurl_net_close(fd); free(scheme); free(host); free(path); return NURL_ERR_TLS_HANDSHAKE; }
    }
    NutStream *stream = nurl_stream_new(fd, t);

    if (common->verbose && !common->silent) {
        if (use_tls) fprintf(stderr, "* Connected to %s:%d (TLS warm)\n", host, port);
        else fprintf(stderr, "* Connected to %s:%d (plaintext)\n", host, port);
    }

    unsigned long *latencies = malloc(sizeof(unsigned long) * count);
    for (unsigned int i = 0; i < count; i++) {
        double start = nurl_utils_get_time_sec();
        nurl_http_response_t *res = NULL;
        NutHttpParams p = { .method = "HEAD", .path = path, .hostname = host, .extra_headers = "Connection: keep-alive\r\n" };
        err = nurl_http_request(stream, &p, &res);

        if (err != NURL_OK) {
            nurl_diag_err("ping: request %u failed (error %d)", i + 1, err);
            latencies[i] = 0;
        } else {
            unsigned long diff = (unsigned long)((nurl_utils_get_time_sec() - start) * 1000.0);
            latencies[i] = diff;
            if (!common->silent) printf("ping %s: seq=%u status=%d time=%lu ms\n", host, i + 1, res->status_code, diff);
            nurl_http_response_free(res);
        }
        if (i < count - 1) nurl_sleep_ms(interval);
    }

    unsigned long min = ULONG_MAX, max = 0, sum = 0, success = 0;
    for (unsigned int i = 0; i < count; i++) {
        if (latencies[i] > 0) {
            if (latencies[i] < min) min = latencies[i];
            if (latencies[i] > max) max = latencies[i];
            sum += latencies[i]; success++;
        }
    }
    if (success > 0 && !common->silent) {
        printf("\n--- %s ping statistics ---\n%u requests, %u success, %u%% packet loss\nround-trip min/avg/max = %lu/%lu/%lu ms\n", host, count, (unsigned int)success, (unsigned int)((count - success) * 100 / count), min, sum / success, max);
    } else if (!common->silent) printf("\n--- %s ping statistics ---\n%u requests, 0 success, 100%% packet loss\n", host, count);

    free(latencies); nurl_tls_free(t); nurl_net_close(fd); nurl_stream_free(stream);
    free(scheme); free(host); free(path);
    return 0;
}

static char *extract_filename_from_cd(const nurl_http_response_t *res) {
    for (size_t i = 0; i < res->header_count; i++) {
        if (nurl_strncasecmp(res->headers[i], "Content-Disposition:", 20) == 0) {
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

static void nurl_download_header_cb(NutRequest *req, const nurl_http_response_t *res, void *user_data) {
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
            nurl_diag_err("could not open '%s' for writing: %s", *dctx->filename_ptr, strerror(errno));
        }
    } else {
        req->out = stdout;
    }
}

static int nurl_mode_download(NutCtx *ctx, const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL, *filename = NULL;
    int port = 0;
    if (nurl_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NURL_ERR_URL;

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
        if (nurl_stat(filename, &st) == 0 && NURL_S_ISREG(st.st_mode)) start_pos = (unsigned long)st.st_size;
    }

    NutRequest *req = nurl_request_new();
    nurl_request_from_args(req, "GET", url, common);
    NutProgressCtx p_ctx = { .resume_offset = start_pos, .silent = common->silent, .start_time = nurl_utils_get_time_sec(), .last_update = 0 };
    if (common->progress) { req->progress_cb = nurl_progress_update; req->progress_data = &p_ctx; }

    DownloadCtx dctx = { .filename_ptr = &filename, .is_stdout = is_stdout, .resume_offset = start_pos, .silent = common->silent };
    req->header_cb = nurl_download_header_cb;
    req->header_data = &dctx;
    req->resume_offset = start_pos;

    if (!common->silent) {
        if (common->output) fprintf(stderr, "* Saving to: %s\n", filename);
        else fprintf(stderr, "* Saving to: %s (will check Content-Disposition)\n", filename);
        if (start_pos > 0) nurl_diag_hint("resuming download from offset: %lu", start_pos);
    }

    nurl_http_response_t *res = NULL;
    char *effective_url = NULL;
    NutOperationStats stats = {0};

    // Note: req->out is opened in the header callback
    int err = execute_with_retry(ctx, req, common, &res, &effective_url, &stats);
    if (req->out && !is_stdout) fclose(req->out);

    if (res && common->dump_header) dump_headers_to_file(common->dump_header, res);

    if (err != NURL_OK && !common->silent) nurl_handle_request_error(err, req, effective_url ? effective_url : url);
    else if (res && !common->silent && res->status_code >= 400) nurl_handle_request_error(res->status_code >= 500 ? NURL_ERR_HTTP_5XX : NURL_ERR_HTTP_4XX, req, effective_url ? effective_url : url);

    nurl_request_free(req); if (res) nurl_http_response_free(res);
    free(effective_url); free(filename); free(scheme); free(host); free(path);
    return err;
}

static int nurl_mode_upload(NutCtx *ctx, const char *url, const CommonArgs *common) {
    if (!common->upload_file) { nurl_diag_err("no upload file specified."); return NURL_ERR_ARG; }
    struct stat st;
    if (nurl_stat(common->upload_file, &st) != 0 || !NURL_S_ISREG(st.st_mode)) { nurl_diag_err("could not read upload file '%s'.", common->upload_file); return NURL_ERR_IO; }

    nurl_http_response_t *res = NULL; char *eff_url = NULL; NutOperationStats stats = {0};
    NutRequest *req = nurl_request_new();
    nurl_request_from_args(req, common->method ? common->method : "POST", url, common);

    int err = execute_with_retry(ctx, req, common, &res, &eff_url, &stats);
    if (res && common->dump_header) dump_headers_to_file(common->dump_header, res);
    if (err != NURL_OK && !common->silent) nurl_handle_request_error(err, req, eff_url ? eff_url : url);
    else if (res && !common->silent && res->status_code >= 400) nurl_handle_request_error(res->status_code >= 500 ? NURL_ERR_HTTP_5XX : NURL_ERR_HTTP_4XX, req, eff_url ? eff_url : url);

    bool should_suppress = (common->fail && res && res->status_code >= 400 && !common->fail_with_body);
    if (err == NURL_OK && res && !common->silent && !should_suppress) {
        if (res->body_len > 0) fwrite(res->body, 1, res->body_len, stdout);
    }
    int ret = (err != NURL_OK) ? err : (res && res->status_code >= 400 ? (res->status_code >= 500 ? NURL_ERR_HTTP_5XX : NURL_ERR_HTTP_4XX) : NURL_OK);
    nurl_request_free(req); if (res) nurl_http_response_free(res); free(eff_url);
    return ret;
}

int nurl_dispatch(NutCtx *ctx, const char *method, const char *url, const CommonArgs *args) {
    if (args->dry_run)  return nurl_mode_inspect(url, args);
    if (args->ping)     return nurl_mode_ping(url, args);
    if (args->resolve)  return nurl_mode_resolve(url, args);
    if (args->download) return nurl_mode_download(ctx, url, args);
    if (args->upload_file) return nurl_mode_upload(ctx, url, args);
    return nurl_request_generic(ctx, method, url, args);
}
