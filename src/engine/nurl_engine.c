#include "nurl_engine.h"
#include "nurl_net.h"
#include "nurl_tls.h"
#include "nurl_pool.h"
#include "nurl_utils.h"
#include "nurl_buf.h"
#include "nurl_http.h"
#include "nurl_cookies.h"
#include "nurl_decompress.h"
#include "nurl_redirect.h"
#include "errors/nurl_diag.h"
#include "compat/nurl_compat.h"
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
    if (nurl_strcasecmp(cookie_domain, host) == 0) return true;
    size_t dlen = strlen(cookie_domain);
    size_t hlen = strlen(host);
    if (hlen > dlen && host[hlen - dlen - 1] == '.') {
        return nurl_strcasecmp(host + hlen - dlen, cookie_domain) == 0;
    }
    return false;
}

static void engine_header_callback(NutHttpParams *p, const nurl_http_response_t *res, void *user_data) {
    NutRequest *req = (NutRequest *)user_data;
    if (req->header_cb) {
        req->header_cb(req, res, req->header_data);
        p->body_out = req->out;
    }
}

int nurl_engine_execute_request(
    NutCtx *ctx,
    NutRequest *req,
    nurl_http_response_t **out_response,
    char **out_effective_url,
    NutOperationStats *out_stats
) {
    if (!req || !req->headers) {
        return NURL_ERR_GENERIC;
    }
    if (out_stats) {
        out_stats->connect_time_sec = 0;
        out_stats->num_redirects = 0;
    }

    req->pool = ctx ? ctx->pool : NULL;

    char *current_url = strdup(req->url);
    int redirects_followed = 0;
    const int max_redirects = req->max_redirects;

    nurl_http_response_t *res = NULL;

    nurl_cookie_jar_t *loaded_jar = NULL;
    bool jar_loaded = false;

    if (req->cookie) {
        if (req->cookie[0] == '@') {
            loaded_jar = nurl_cookie_jar_load(req->cookie + 1);
            if (loaded_jar) jar_loaded = true;
        }
    }

    if (req->session) {
        nurl_cookie_jar_t *s_jar = nurl_cookie_jar_load(req->session);
        if (s_jar) {
            if (jar_loaded) {
                for (size_t i = 0; i < s_jar->count; i++) {
                    nurl_cookie_jar_add(loaded_jar, &s_jar->cookies[i]);
                }
                nurl_cookie_jar_free(s_jar);
            } else {
                loaded_jar = s_jar;
                jar_loaded = true;
            }
        }
    }

    const char *save_path = req->cookie_jar ? req->cookie_jar : req->session;
    if (save_path && !loaded_jar) {
        loaded_jar = nurl_cookie_jar_load(save_path);
        if (!loaded_jar) {
            loaded_jar = nurl_cookie_jar_create();
        }
        if (loaded_jar) jar_loaded = true;
    }

    int return_code = NURL_OK;

    while (true) {
        char *scheme = NULL;
        char *host = NULL;
        char *path = NULL;
        int port = 0;

        if (nurl_utils_parse_url(current_url, &scheme, &host, &port, &path) != 0) {
            free(current_url);
            return_code = NURL_ERR_URL;
            goto cleanup_jar;
        }

        bool use_tls = (nurl_strcasecmp(scheme, "https") == 0);

        int sock_fd = -1;
        nurl_tls_t *tls = NULL;
        NutStream *stream = NULL;
        nurl_err_t conn_err = NURL_OK;

        if (req->pool) {
            nurl_err_t acquire_err = nurl_pool_acquire(req->pool, host, port, use_tls, req, &stream);
            if (acquire_err != NURL_OK) {
                free(scheme); free(host); free(path); free(current_url);
                return_code = acquire_err;
                goto cleanup_jar;
            }
            tls = stream->tls;
            sock_fd = stream->fd;
        } else {
            double conn_start = nurl_utils_get_time_sec();
            // Stage 1: Connect (TCP + Proxy Tunneling)
            sock_fd = nurl_net_connect_proxy_ex(host, port, req->proxy, req->proxy_user, req->no_proxy, req->connect_to, req->connect_timeout_sec, &conn_err);
            if (sock_fd < 0) {
                free(scheme); free(host); free(path); free(current_url);
                return_code = conn_err;
                goto cleanup_jar;
            }
            if (out_stats && redirects_followed == 0) {
                out_stats->connect_time_sec = nurl_utils_get_time_sec() - conn_start;
            }

            if (req->read_timeout_sec > 0) {
                nurl_net_set_timeout(sock_fd, req->read_timeout_sec);
            }

            if (use_tls) {
                // Stage 2: TLS Creation
                tls = nurl_tls_create(req->tls_verify, req->cacert, req->cert, req->key, req->tls_version == 12, req->tls_version == 13);
                if (!tls) {
                    nurl_net_close(sock_fd);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NURL_ERR_TLS;
                    goto cleanup_jar;
                }

                // Stage 3: TLS Handshake
                if (nurl_tls_handshake(tls, sock_fd, host) != 0) {
                    const char *tls_err = nurl_tls_last_error(tls);
                    if (tls_err) {
                        snprintf(req->last_tls_error, sizeof(req->last_tls_error), "%s", tls_err);
                    }
                    nurl_tls_free(tls);
                    nurl_net_close(sock_fd);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NURL_ERR_TLS_HANDSHAKE;
                    goto cleanup_jar;
                }
            }

            stream = nurl_stream_new(sock_fd, tls);
            if (!stream) {
                if (tls) nurl_tls_free(tls);
                nurl_net_close(sock_fd);
                free(scheme); free(host); free(path); free(current_url);
                return_code = NURL_ERR_OOM;
                goto cleanup_jar;
            }
        }

        if (stream) {
            nurl_stream_set_limit_rate(stream, req->limit_rate);
        }

        NutHeaderMap *temp_hdrs = nurl_headermap_new();
        if (!temp_hdrs) {
            if (req->pool) {
                nurl_pool_evict(req->pool, stream);
            } else {
                nurl_tls_free(tls);
                nurl_net_close(sock_fd);
                nurl_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = NURL_ERR_OOM;
            goto cleanup_jar;
        }

        // Copy base headers
        for (size_t i = 0; i < req->headers->count; i++) {
            nurl_headermap_append(temp_hdrs, req->headers->keys[i], req->headers->values[i]);
        }

        if (req->resume_offset > 0 && !nurl_headermap_has(temp_hdrs, "Range")) {
            char range_val[64];
            snprintf(range_val, sizeof(range_val), "bytes=%lu-", req->resume_offset);
            nurl_headermap_set(temp_hdrs, "Range", range_val);
        }

        // Dynamic Cookie compilation
        NutBuf cookie_buf;
        nurl_buf_init(&cookie_buf);

        if (req->cookie && req->cookie[0] != '@') {
            nurl_buf_append(&cookie_buf, req->cookie, strlen(req->cookie));
        }

        if (jar_loaded && loaded_jar) {
            unsigned long now_time = (unsigned long)time(NULL);
            for (size_t i = 0; i < loaded_jar->count; i++) {
                nurl_cookie_t *c = &loaded_jar->cookies[i];
                if (c->expiry != 0 && c->expiry < now_time) {
                    continue;
                }
                if (c->domain && !cookie_domain_matches(c->domain, host)) {
                    continue;
                }
                if (cookie_buf.len > 0) nurl_buf_append(&cookie_buf, "; ", 2);
                nurl_buf_append(&cookie_buf, c->name, strlen(c->name));
                nurl_buf_append(&cookie_buf, "=", 1);
                nurl_buf_append(&cookie_buf, c->value, strlen(c->value));
            }
        }

        if (cookie_buf.len > 0) {
            char *cookie_str = nurl_buf_take(&cookie_buf);
            if (cookie_str && !nurl_headermap_has(temp_hdrs, "Cookie")) {
                nurl_headermap_set(temp_hdrs, "Cookie", cookie_str);
            }
            free(cookie_str);
        } else {
            nurl_buf_free(&cookie_buf);
        }

        char *extra_hdr = nurl_headermap_serialize(temp_hdrs);
        nurl_headermap_free(temp_hdrs);

        if (!extra_hdr) {
            if (req->pool) {
                nurl_pool_evict(req->pool, stream);
            } else {
                if (tls) nurl_tls_free(tls);
                nurl_net_close(sock_fd);
                nurl_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = NURL_ERR_OOM;
            goto cleanup_jar;
        }

        const char *proto = tls ? nurl_tls_get_negotiated_proto(tls) : "none";
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
                    const char *redacted = nurl_utils_redact_header(key, val);
                    fprintf(stderr, "> %s: %s\n", key, redacted);
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
            free(hdr_copy);
            fprintf(stderr, "> \n");
        }

        NutHttpParams http_params = {0};
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

        nurl_err_t http_err = nurl_http_request(stream, &http_params, &res);
        free(extra_hdr);

        if (http_err != NURL_OK) {
            if (req->pool) {
                nurl_pool_evict(req->pool, stream);
            } else {
                if (tls) nurl_tls_free(tls);
                nurl_net_close(sock_fd);
                nurl_stream_free(stream);
            }
            free(scheme); free(host); free(path); free(current_url);
            return_code = http_err;
            goto cleanup_jar;
        }

        // Handle decompression if Accept-Encoding was requested
        if (req->decompress && res && res->body_len > 0) {
            bool is_compressed = false;
            for (size_t i = 0; i < res->header_count; i++) {
                if (nurl_strncasecmp(res->headers[i], "Content-Encoding:", 17) == 0) {
                    char *encoding = res->headers[i] + 17;
                    while (*encoding && isspace((unsigned char)*encoding)) encoding++;
                    if (nurl_strcasecmp(encoding, "gzip") == 0 || nurl_strcasecmp(encoding, "deflate") == 0) {
                        is_compressed = true;
                        break;
                    }
                }
            }
            if (is_compressed) {
                size_t decompressed_len = 0;
                unsigned char *decompressed = nurl_decompress_gzip_deflate(
                    (const unsigned char *)res->body, res->body_len, &decompressed_len
                );
                if (decompressed) {
                    free(res->body);
                    res->body = decompressed;
                    res->body_len = decompressed_len;
                } else {
                    nurl_diag_err("Failed to decompress response payload.");
                    nurl_http_response_free(res);
                    free(scheme); free(host); free(path); free(current_url);
                    return_code = NURL_ERR_GENERIC;
                    goto cleanup_jar;
                }
            }
        }

        // Extract Set-Cookie headers and save to jar
        if (save_path && res && loaded_jar) {
            for (size_t i = 0; i < res->header_count; i++) {
                if (nurl_strncasecmp(res->headers[i], "Set-Cookie:", 11) == 0) {
                    char *val = strdup(res->headers[i] + 11);
                    if (!val) continue;

                    char *parts[32];
                    size_t part_count = 0;
                    char *tok = val;
                    while (part_count < 32) {
                        char *semi = strchr(tok, ';');
                        if (semi) {
                            *semi = '\0';
                            parts[part_count++] = tok;
                            tok = semi + 1;
                        } else {
                            parts[part_count++] = tok;
                            break;
                        }
                    }

                    if (part_count > 0) {
                        char *eq = strchr(parts[0], '=');
                        if (eq) {
                            *eq = '\0';
                            char *name = nurl_utils_trim(parts[0]);
                            char *value = nurl_utils_trim(eq + 1);
                            char *domain = NULL;
                            char *cookie_path = NULL;
                            bool secure = false;

                            for (size_t p = 1; p < part_count; p++) {
                                char *attr = parts[p];
                                char *attr_eq = strchr(attr, '=');
                                if (attr_eq) {
                                    *attr_eq = '\0';
                                    char *k_attr = nurl_utils_trim(attr);
                                    char *v_attr = nurl_utils_trim(attr_eq + 1);
                                    if (nurl_strcasecmp(k_attr, "domain") == 0) {
                                        if (domain) free(domain);
                                        domain = strdup(v_attr);
                                    } else if (nurl_strcasecmp(k_attr, "path") == 0) {
                                        if (cookie_path) free(cookie_path);
                                        cookie_path = strdup(v_attr);
                                    }
                                } else {
                                    char *k_attr = nurl_utils_trim(attr);
                                    if (nurl_strcasecmp(k_attr, "secure") == 0) {
                                        secure = true;
                                    }
                                }
                            }

                            if (!domain) {
                                domain = strdup(host);
                            }
                            if (!cookie_path) {
                                cookie_path = strdup("/");
                            }

                            nurl_cookie_t c;
                            c.domain = domain;
                            c.include_subdomains = true;
                            c.path = cookie_path;
                            c.secure = secure;
                            c.expiry = 0;
                            c.name = name;
                            c.value = value;

                            nurl_cookie_jar_add(loaded_jar, &c);

                            free(domain);
                            free(cookie_path);
                        }
                    }
                    free(val);
                }
            }
            nurl_cookie_jar_save(loaded_jar, save_path);
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
                if (nurl_strncasecmp(res->headers[i], "Location:", 9) == 0) {
                    char *val = res->headers[i] + 9;
                    while (*val && isspace((unsigned char)*val)) val++;
                    redir_url = nurl_resolve_redirect(current_url, val);
                    break;
                }
            }

            if (redir_url) {
                bool keep_alive = true;
                for (size_t i = 0; i < res->header_count; i++) {
                    if (nurl_strncasecmp(res->headers[i], "Connection:", 11) == 0) {
                        char *val = res->headers[i] + 11;
                        while (*val && isspace((unsigned char)*val)) val++;
                        if (nurl_strcasecmp(val, "close") == 0) { keep_alive = false; }
                        break;
                    }
                }
                nurl_http_response_free(res);
                if (req->pool) {
                    if (keep_alive) nurl_pool_release(req->pool, host, port, stream);
                    else nurl_pool_evict(req->pool, stream);
                } else {
                    if (tls) nurl_tls_free(tls);
                    nurl_net_close(sock_fd);
                    nurl_stream_free(stream);
                }
                free(scheme); free(host); free(path);

                free(current_url);
                current_url = redir_url;
                redirects_followed++;

                if (redirects_followed >= max_redirects) {
                    nurl_diag_err("maximum redirect limit exceeded (%d).", max_redirects);
                    free(current_url);
                    return_code = NURL_ERR_GENERIC;
                    goto cleanup_jar;
                }
                continue;
            }
        }

        if (req->pool) {
            bool keep_alive = true;
            if (res) {
                for (size_t i = 0; i < res->header_count; i++) {
                    if (nurl_strncasecmp(res->headers[i], "Connection:", 11) == 0) {
                        char *val = res->headers[i] + 11;
                        while (*val && isspace((unsigned char)*val)) val++;
                        if (nurl_strcasecmp(val, "close") == 0) {
                            keep_alive = false;
                        }
                        break;
                    }
                }
            } else {
                keep_alive = false;
            }
            if (keep_alive) {
                nurl_pool_release(req->pool, host, port, stream);
            } else {
                nurl_pool_evict(req->pool, stream);
            }
        } else {
            if (tls) nurl_tls_free(tls);
            nurl_net_close(sock_fd);
            nurl_stream_free(stream);
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
    if (loaded_jar) nurl_cookie_jar_free(loaded_jar);

    return return_code;
}
