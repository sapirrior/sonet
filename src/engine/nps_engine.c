#include "nps_engine.h"
#include "nps_net.h"
#include "nps_tls.h"
#include "nps_pool.h"
#include "nps_utils.h"
#include "nps_buf.h"
#include "nps_http.h"
#include "nps_cookies.h"
#include "nps_decompress.h"
#include "nps_redirect.h"
#include "errors/nps_diag.h"
#include "compat/nps_compat.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>

// Simple suffix match — domain "example.com" matches host "api.example.com"
static bool cookie_domain_matches(const char *cookie_domain, const char *host) {
    if (!cookie_domain || !host) return false;
    if (nps_strcasecmp(cookie_domain, host) == 0) return true;
    size_t dlen = strlen(cookie_domain);
    size_t hlen = strlen(host);
    if (hlen > dlen && host[hlen - dlen - 1] == '.') {
        return nps_strcasecmp(host + hlen - dlen, cookie_domain) == 0;
    }
    return false;
}

static void engine_header_callback(NpsHttpParams *p, const nps_http_response_t *res, void *user_data) {
    NpsRequest *req = (NpsRequest *)user_data;
    if (req->header_cb) {
        req->header_cb(req, res, req->header_data);
        p->body_out = req->out;
    }
}

int nps_engine_execute_request(
    NpsCtx *ctx,
    NpsRequest *req,
    nps_http_response_t **out_response,
    char **out_effective_url,
    NpsOperationStats *out_stats
) {
    if (!req || !req->headers) {
        return NPS_ERR_GENERIC;
    }
    if (out_stats) {
        out_stats->connect_time_sec = 0;
        out_stats->num_redirects = 0;
    }

    req->pool = ctx ? ctx->pool : NULL;

    char *current_url = strdup(req->url);
    int redirects_followed = 0;
    const int max_redirects = req->max_redirects;

    nps_http_response_t *res = NULL;

    nps_cookie_jar_t *loaded_jar = NULL;
    bool jar_loaded = false;

    if (req->cookie) {
        if (req->cookie[0] == '@') {
            loaded_jar = nps_cookie_jar_load(req->cookie + 1);
            if (loaded_jar) jar_loaded = true;
        }
    }

    if (req->session) {
        nps_cookie_jar_t *s_jar = nps_cookie_jar_load(req->session);
        if (s_jar) {
            if (jar_loaded) {
                for (size_t i = 0; i < s_jar->count; i++) {
                    nps_cookie_jar_add(loaded_jar, &s_jar->cookies[i]);
                }
                nps_cookie_jar_free(s_jar);
            } else {
                loaded_jar = s_jar;
                jar_loaded = true;
            }
        }
    }

    const char *save_path = req->cookie_jar ? req->cookie_jar : req->session;
    if (save_path && !loaded_jar) {
        loaded_jar = nps_cookie_jar_load(save_path);
        if (!loaded_jar) {
            loaded_jar = nps_cookie_jar_create();
        }
        if (loaded_jar) jar_loaded = true;
    }

    int return_code = NPS_OK;

    while (true) {
        char *scheme = NULL;
        char *host = NULL;
        char *path = NULL;
        int port = 0;

        if (nps_utils_parse_url(current_url, &scheme, &host, &port, &path) != 0) {
            free(current_url);
            return_code = NPS_ERR_URL;
            goto cleanup_jar;
        }

        bool use_tls = (nps_strcasecmp(scheme, "https") == 0);

        int sock_fd = -1;
        nps_tls_t *tls = NULL;
        NpsStream *stream = NULL;
        nps_err_t conn_err = NPS_OK;

        if (req->pool) {
            nps_err_t acquire_err = nps_pool_acquire(req->pool, host, port, use_tls, req, &stream);
            if (acquire_err != NPS_OK) {
                free(scheme); free(host); free(path); free(current_url);
                return_code = acquire_err;
                goto cleanup_jar;
            }
            tls = stream->tls;
            sock_fd = stream->fd;
        } else {
            double conn_start = nps_utils_get_time_sec();
            // Stage 1: Connect (TCP + Proxy Tunneling)
            sock_fd = nps_net_connect_proxy_ex(host, port, req->proxy, req->proxy_user, req->no_proxy, req->connect_to, req->connect_timeout_sec, &conn_err);
            if (sock_fd < 0) {
                free(scheme); free(host); free(path); free(current_url);
                return_code = conn_err;
                goto cleanup_jar;
            }
            if (out_stats && redirects_followed == 0) {
                out_stats->connect_time_sec = nps_utils_get_time_sec() - conn_start;
            }

            if (req->read_timeout_sec > 0) {
                nps_net_set_timeout(sock_fd, req->read_timeout_sec);
            }

            if (use_tls) {
                // Stage 2: TLS Creation
                tls = nps_tls_create(req->tls_verify, req->cacert, req->cert, req->key, req->tls_version == 12, req->tls_version == 13);
                if (!tls) {
                    nps_net_close(sock_fd);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NPS_ERR_TLS;
                    goto cleanup_jar;
                }

                // Stage 3: TLS Handshake
                if (nps_tls_handshake(tls, sock_fd, host) != 0) {
                    const char *tls_err = nps_tls_last_error(tls);
                    if (tls_err) {
                        snprintf(req->last_tls_error, sizeof(req->last_tls_error), "%s", tls_err);
                    }
                    nps_tls_free(tls);
                    nps_net_close(sock_fd);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NPS_ERR_TLS_HANDSHAKE;
                    goto cleanup_jar;
                }
            }

            stream = nps_stream_new(sock_fd, tls);
            if (!stream) {
                if (tls) nps_tls_free(tls);
                nps_net_close(sock_fd);
                free(scheme); free(host); free(path); free(current_url);
                return_code = NPS_ERR_OOM;
                goto cleanup_jar;
            }
        }

        if (stream) {
            nps_stream_set_limit_rate(stream, req->limit_rate);
        }

        NpsHeaderMap *temp_hdrs = nps_headermap_new();
        if (!temp_hdrs) {
            if (req->pool) {
                nps_pool_evict(req->pool, stream);
            } else {
                nps_tls_free(tls);
                nps_net_close(sock_fd);
                nps_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = NPS_ERR_OOM;
            goto cleanup_jar;
        }

        // Copy base headers
        for (size_t i = 0; i < req->headers->count; i++) {
            nps_headermap_append(temp_hdrs, req->headers->keys[i], req->headers->values[i]);
        }

        if (req->resume_offset > 0 && !nps_headermap_has(temp_hdrs, "Range")) {
            char range_val[64];
            snprintf(range_val, sizeof(range_val), "bytes=%lu-", req->resume_offset);
            nps_headermap_set(temp_hdrs, "Range", range_val);
        }

        // Dynamic Cookie compilation
        NpsBuf cookie_buf;
        nps_buf_init(&cookie_buf);

        if (req->cookie && req->cookie[0] != '@') {
            nps_buf_append(&cookie_buf, req->cookie, strlen(req->cookie));
        }

        if (jar_loaded && loaded_jar) {
            unsigned long now_time = (unsigned long)time(NULL);
            for (size_t i = 0; i < loaded_jar->count; i++) {
                nps_cookie_t *c = &loaded_jar->cookies[i];
                if (c->expiry != 0 && c->expiry < now_time) {
                    continue;
                }
                if (c->domain && !cookie_domain_matches(c->domain, host)) {
                    continue;
                }
                if (cookie_buf.len > 0) nps_buf_append(&cookie_buf, "; ", 2);
                nps_buf_append(&cookie_buf, c->name, strlen(c->name));
                nps_buf_append(&cookie_buf, "=", 1);
                nps_buf_append(&cookie_buf, c->value, strlen(c->value));
            }
        }

        if (cookie_buf.len > 0) {
            char *cookie_str = nps_buf_take(&cookie_buf);
            if (cookie_str && !nps_headermap_has(temp_hdrs, "Cookie")) {
                nps_headermap_set(temp_hdrs, "Cookie", cookie_str);
            }
            free(cookie_str);
        } else {
            nps_buf_free(&cookie_buf);
        }

        char *extra_hdr = nps_headermap_serialize(temp_hdrs);
        nps_headermap_free(temp_hdrs);

        if (!extra_hdr) {
            if (req->pool) {
                nps_pool_evict(req->pool, stream);
            } else {
                if (tls) nps_tls_free(tls);
                nps_net_close(sock_fd);
                nps_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = NPS_ERR_OOM;
            goto cleanup_jar;
        }

        const char *proto = tls ? nps_tls_get_negotiated_proto(tls) : "none";
        if (req->verbose && !req->silent) {
            fprintf(stderr, "* Connected to %s port %d\n", host, port);
            if (tls) fprintf(stderr, "* TLS handshake complete (ALPN: %s)\n*\n", proto ? proto : "none");
            else fprintf(stderr, "* Connected via plaintext HTTP\n*\n");
            fprintf(stderr, "> %s %s HTTP/1.1\n", req->method, path);
            fprintf(stderr, "> Host: %s\n", host);
            fprintf(stderr, "> Connection: close\n");

            char *hdr_copy = strdup(extra_hdr);
            char *saveptr = NULL;
            char *line = strtok_r(hdr_copy, "\r\n", &saveptr);
            while (line) {
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    char *key = line;
                    char *val = colon + 1;
                    while (*val && isspace((unsigned char)*val)) val++;
                    const char *redacted = nps_utils_redact_header(key, val);
                    fprintf(stderr, "> %s: %s\n", key, redacted);
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
            free(hdr_copy);
            fprintf(stderr, "> \n");
        }

        NpsHttpParams http_params = {0};
        http_params.method = req->method;
        http_params.path = path;
        http_params.hostname = host;
        http_params.extra_headers = extra_hdr;
        http_params.body = req->body;
        http_params.body_len = req->body_len;
        http_params.body_parts = req->body_parts;
        http_params.body_parts_count = req->body_parts_count;
        http_params.body_out = req->out;
        http_params.resume_offset = req->resume_offset;
        http_params.progress_cb = req->progress_cb;
        http_params.progress_data = req->progress_data;
        http_params.header_cb = engine_header_callback;
        http_params.header_data = req;
        http_params.http10 = req->http10;

        nps_err_t http_err = nps_http_request(stream, &http_params, &res);
        free(extra_hdr);

        if (http_err != NPS_OK) {
            if (req->pool) {
                nps_pool_evict(req->pool, stream);
            } else {
                if (tls) nps_tls_free(tls);
                nps_net_close(sock_fd);
                nps_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = http_err;
            goto cleanup_jar;
        }

        // Handle decompression if Accept-Encoding was requested
        if (req->decompress && res && res->body_len > 0) {
            bool is_compressed = false;
            for (size_t i = 0; i < res->header_count; i++) {
                if (nps_strncasecmp(res->headers[i], "Content-Encoding:", 17) == 0) {
                    char *encoding = res->headers[i] + 17;
                    while (*encoding && isspace((unsigned char)*encoding)) encoding++;
                    if (nps_strcasecmp(encoding, "gzip") == 0 || nps_strcasecmp(encoding, "deflate") == 0) {
                        is_compressed = true;
                        break;
                    }
                }
            }
            if (is_compressed) {
                size_t decompressed_len = 0;
                unsigned char *decompressed = nps_decompress_gzip_deflate(
                    (const unsigned char *)res->body, res->body_len, &decompressed_len
                );
                if (decompressed) {
                    free(res->body);
                    res->body = decompressed;
                    res->body_len = decompressed_len;
                } else {
                    nps_diag_err("Failed to decompress response payload.");
                    nps_http_response_free(res);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NPS_ERR_GENERIC;
                    goto cleanup_jar;
                }
            }
        }

        // Extract Set-Cookie headers and save to jar
        if (save_path && res && loaded_jar) {
            for (size_t i = 0; i < res->header_count; i++) {
                if (nps_strncasecmp(res->headers[i], "Set-Cookie:", 11) == 0) {
                    nps_cookie_parse_set_cookie(loaded_jar, res->headers[i] + 11, host);
                }
            }
            nps_cookie_jar_save(loaded_jar, save_path);
        }

        if (req->verbose && !req->silent) {
            fprintf(stderr, "< HTTP/1.1 %d %s\n", res->status_code, res->status_text);
            for (size_t i = 0; i < res->header_count; i++) {
                fprintf(stderr, "< %s\n", res->headers[i]);
            }
            fprintf(stderr, "< \n");
        }

        if (req->follow_redirect && res->status_code >= 300 && res->status_code < 400) {
            char *redir_url = NULL;
            for (size_t i = 0; i < res->header_count; i++) {
                if (nps_strncasecmp(res->headers[i], "Location:", 9) == 0) {
                    char *val = res->headers[i] + 9;
                    while (*val && isspace((unsigned char)*val)) val++;
                    redir_url = nps_resolve_redirect(current_url, val);
                    break;
                }
            }

            if (redir_url) {
                bool keep_alive = true;
                for (size_t i = 0; i < res->header_count; i++) {
                    if (nps_strncasecmp(res->headers[i], "Connection:", 11) == 0) {
                        char *val = res->headers[i] + 11;
                        while (*val && isspace((unsigned char)*val)) val++;
                        if (nps_strcasecmp(val, "close") == 0) { keep_alive = false; }
                        break;
                    }
                }
                nps_http_response_free(res);
                if (req->pool) {
                    if (keep_alive) nps_pool_release(req->pool, host, port, stream);
                    else nps_pool_evict(req->pool, stream);
                } else {
                    if (tls) nps_tls_free(tls);
                    nps_net_close(sock_fd);
                    nps_stream_free(stream);
                }
                free(scheme); free(host); free(path);

                free(current_url);
                current_url = redir_url;
                redirects_followed++;

                if (redirects_followed >= max_redirects) {
                    nps_diag_err("maximum redirect limit exceeded (%d).", max_redirects);
                    free(current_url);
                    return_code = NPS_ERR_GENERIC;
                    goto cleanup_jar;
                }
                continue;
            }
        }

        if (req->pool) {
            bool keep_alive = true;
            if (res) {
                for (size_t i = 0; i < res->header_count; i++) {
                    if (nps_strncasecmp(res->headers[i], "Connection:", 11) == 0) {
                        char *val = res->headers[i] + 11;
                        while (*val && isspace((unsigned char)*val)) val++;
                        if (nps_strcasecmp(val, "close") == 0) {
                            keep_alive = false;
                        }
                        break;
                    }
                }
            } else {
                keep_alive = false;
            }
            if (keep_alive) {
                nps_pool_release(req->pool, host, port, stream);
            } else {
                nps_pool_evict(req->pool, stream);
            }
        } else {
            if (tls) nps_tls_free(tls);
            nps_net_close(sock_fd);
            nps_stream_free(stream);
        }
        free(scheme); free(host); free(path);
        break;
    }

    *out_response = res;
    if (out_effective_url) {
        *out_effective_url = current_url;
    } else {
        free(current_url);
    }

    if (out_stats) {
        out_stats->num_redirects = redirects_followed;
    }

cleanup_jar:
    if (loaded_jar) nps_cookie_jar_free(loaded_jar);

    return return_code;
}
