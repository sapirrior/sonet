#include "nps_request.h"
#include "nps_engine.h"
#include "nps_engine_request.h"
#include "nps_retry.h"
#include "nps_progress.h"
#include "net/nps_stream.h"
#include "net/nps_net.h"
#include "utils/nps_utils.h"
#include "utils/nps_buf.h"
#include "errors/nps_diag.h"
#include "errors/nps_error_handler.h"
#include "compat/nps_compat.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

/* Platform-portable way to get remote peer info — uses nps_net layer */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void handle_write_out(const char *template, const nps_http_response_t *res, const char *method, const char *url, double elapsed_sec, const NpsOperationStats *stats, NpsRequest *req) {
    if (!template) return;

    NpsBuf b;
    nps_buf_init(&b);

    const char *p = template;
    while (*p) {
        if (*p == '%' && *(p + 1) == '{') {
            const char *start = p + 2;
            const char *end = strchr(start, '}');
            if (end) {
                size_t key_len = end - start;
                if (strncmp(start, "http_code", key_len) == 0) {
                    nps_buf_printf(&b, "%d", res->status_code);
                } else if (strncmp(start, "time_total", key_len) == 0) {
                    nps_buf_printf(&b, "%.3f", elapsed_sec);
                } else if (strncmp(start, "time_connect", key_len) == 0) {
                    nps_buf_printf(&b, "%.3f", stats->connect_time_sec);
                } else if (strncmp(start, "size_download", key_len) == 0) {
                    nps_buf_printf(&b, "%zu", res->body_len);
                } else if (strncmp(start, "url_effective", key_len) == 0) {
                    nps_buf_append(&b, url, strlen(url));
                } else if (strncmp(start, "num_redirects", key_len) == 0) {
                    nps_buf_printf(&b, "%d", stats->num_redirects);
                } else if (strncmp(start, "method", key_len) == 0) {
                    nps_buf_append(&b, method, strlen(method));
                } else if (strncmp(start, "remote_ip", key_len) == 0 || strncmp(start, "remote_port", key_len) == 0) {
                    if (req && req->stream) {
                        struct sockaddr_storage addr;
                        socklen_t addr_len = sizeof(addr);
                        if (getpeername(req->stream->fd, (struct sockaddr *)&addr, &addr_len) == 0) {
                            if (addr.ss_family == AF_INET) {
                                struct sockaddr_in *s = (struct sockaddr_in *)&addr;
                                if (strncmp(start, "remote_ip", key_len) == 0) {
                                    char ip[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip));
                                    nps_buf_append(&b, ip, strlen(ip));
                                } else {
                                    nps_buf_printf(&b, "%d", ntohs(s->sin_port));
                                }
                            } else if (addr.ss_family == AF_INET6) {
                                struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
                                if (strncmp(start, "remote_ip", key_len) == 0) {
                                    char ip[INET6_ADDRSTRLEN];
                                    inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip));
                                    nps_buf_append(&b, ip, strlen(ip));
                                } else {
                                    nps_buf_printf(&b, "%d", ntohs(s->sin6_port));
                                }
                            }
                        }
                    }
                } else if (strncmp(start, "content_type", key_len) == 0) {
                    const char *content_type = "";
                    for (size_t i = 0; i < res->header_count; i++) {
                        if (nps_strncasecmp(res->headers[i], "Content-Type:", 13) == 0) {
                            content_type = res->headers[i] + 13;
                            while (*content_type && isspace((unsigned char)*content_type)) content_type++;
                            break;
                        }
                    }
                    nps_buf_append(&b, content_type, strlen(content_type));
                } else if (strncmp(start, "scheme", key_len) == 0 || strncmp(start, "host", key_len) == 0) {
                    char *scheme = NULL, *host = NULL, *path = NULL;
                    int port = 0;
                    nps_utils_parse_url(url, &scheme, &host, &port, &path);
                    if (strncmp(start, "scheme", key_len) == 0) nps_buf_append(&b, scheme ? scheme : "", scheme ? strlen(scheme) : 0);
                    else nps_buf_append(&b, host ? host : "", host ? strlen(host) : 0);
                    free(scheme); free(host); free(path);
                } else {
                    // Unknown variable, just append as is
                    nps_buf_append(&b, p, (size_t)(end - p + 1));
                }
                p = end + 1;
                continue;
            }
        } else if (*p == '\\') {
            if (*(p + 1) == 'n') { nps_buf_append(&b, "\n", 1); p += 2; continue; }
            else if (*(p + 1) == 't') { nps_buf_append(&b, "\t", 1); p += 2; continue; }
        }
        nps_buf_append(&b, p, 1);
        p++;
    }

    char *out = nps_buf_take(&b);
    printf("%s", out);
    free(out);
}

void dump_headers_to_file(const char *filename, const nps_http_response_t *res) {
    if (!filename || !res) return;
    FILE *hf = fopen(filename, "w");
    if (hf) {
        fprintf(hf, "HTTP/1.1 %d %s\r\n", res->status_code, res->status_text);
        for (size_t i = 0; i < res->header_count; i++) {
            fprintf(hf, "%s\r\n", res->headers[i]);
        }
        fprintf(hf, "\r\n");
        fclose(hf);
    } else {
        nps_diag_warn("could not open header dump file '%s': %s", filename, strerror(errno));
    }
}

int nps_request_generic(NpsCtx *ctx, const char *method, const char *url, const CommonArgs *common) {
    double start_time = nps_utils_get_time_sec();

    nps_http_response_t *res = NULL;
    char *effective_url = NULL;
    NpsOperationStats stats = {0};

    NpsRequest *req = nps_request_new();
    if (!req) return NPS_ERR_OOM;
    nps_request_from_args(req, method, url, common);

    NpsProgressCtx p_ctx;
    if (common->progress) {
        p_ctx.resume_offset = 0;
        p_ctx.silent = common->silent;
        p_ctx.start_time = nps_utils_get_time_sec();
        p_ctx.last_update = p_ctx.start_time;
        req->progress_cb = nps_progress_update;
        req->progress_data = &p_ctx;
    }

    int engine_err = execute_with_retry(ctx, req, common, &res, &effective_url, &stats);

    if (engine_err != NPS_OK) {
        if (!common->silent) {
            nps_handle_request_error(engine_err, req, effective_url ? effective_url : url);
        }
        nps_request_free(req);
        if (effective_url) free(effective_url);
        return engine_err;
    }

    if (!res) {
        nps_request_free(req);
        if (effective_url) free(effective_url);
        return NPS_ERR_GENERIC;
    }

    if (common->dump_header) {
        dump_headers_to_file(common->dump_header, res);
    }

    bool is_error_status = (res->status_code >= 400);
    bool should_suppress_output = (common->fail && is_error_status && !common->fail_with_body);

    if (!should_suppress_output) {
        if (common->include && !common->verbose) {
            printf("HTTP/1.1 %d %s\n", res->status_code, res->status_text);
            for (size_t i = 0; i < res->header_count; i++) {
                printf("%s\n", res->headers[i]);
            }
            printf("\n");
        }

        if (common->output) {
            bool is_stdout = (strcmp(common->output, "-") == 0);
            FILE *f = is_stdout ? stdout : fopen(common->output, "wb");
            if (!f) {
                nps_diag_err("could not open '%s' for writing: %s", common->output, strerror(errno));
                nps_request_free(req);
                nps_http_response_free(res);
                free(effective_url);
                return NPS_ERR_IO;
            }
            if (res->body_len > 0) {
                fwrite(res->body, 1, res->body_len, f);
            }
            if (!is_stdout) fclose(f);
            else fflush(f);
        } else {
            if (res->body_len > 0) {
                fwrite(res->body, 1, res->body_len, stdout);
            }
        }
    }

    double elapsed_sec = nps_utils_get_time_sec() - start_time;

    if (common->write_out && !should_suppress_output) {
        /* req must be alive here for remote_ip/remote_port lookups */
        handle_write_out(common->write_out, res, method, effective_url ? effective_url : url, elapsed_sec, &stats, req);
    }

    /* Free req AFTER handle_write_out, which may access req->stream */
    nps_request_free(req);

    int ret_code = NPS_OK;
    if (res->status_code >= 500) {
        ret_code = NPS_ERR_HTTP_5XX;
    } else if (res->status_code >= 400) {
        ret_code = NPS_ERR_HTTP_4XX;
    }

    nps_http_response_free(res);
    if (effective_url) free(effective_url);
    return ret_code;
}
