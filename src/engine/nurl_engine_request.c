#include "nurl_engine_request.h"
#include "nurl_multipart.h"
#include "nurl_buf.h"
#include "utils/nurl_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

NutRequest *nurl_request_new(void) {
    NutRequest *req = calloc(1, sizeof(NutRequest));
    if (!req) return NULL;
    req->max_redirects = 10;
    return req;
}

nurl_err_t nurl_headermap_apply_auth(NutHeaderMap *m, const CommonArgs *a) {
    if (!m || !a) return NURL_ERR_GENERIC;
    if (a->no_auth) return NURL_OK;

    if (nurl_headermap_has(m, "Authorization")) {
        return NURL_OK;
    }

    if (a->bearer || a->token) {
        const char *tok = a->bearer ? a->bearer : a->token;
        NutBuf auth_val;
        nurl_buf_init(&auth_val);
        if (!nurl_buf_printf(&auth_val, "Bearer %s", tok)) {
            nurl_buf_free(&auth_val);
            return NURL_ERR_OOM;
        }
        nurl_err_t err = nurl_headermap_set(m, "Authorization", auth_val.data);
        nurl_buf_free(&auth_val);
        return err;
    } else if (a->user) {
        char *b64 = nurl_utils_base64_encode((const unsigned char *)a->user, strlen(a->user));
        if (!b64) return NURL_ERR_OOM;
        
        NutBuf auth_val;
        nurl_buf_init(&auth_val);
        if (!nurl_buf_printf(&auth_val, "Basic %s", b64)) {
            free(b64);
            nurl_buf_free(&auth_val);
            return NURL_ERR_OOM;
        }
        free(b64);
        nurl_err_t err = nurl_headermap_set(m, "Authorization", auth_val.data);
        nurl_buf_free(&auth_val);
        return err;
    }

    return NURL_OK;
}

nurl_err_t nurl_headermap_apply_common(NutHeaderMap *m, const CommonArgs *a) {
    if (!m || !a) return NURL_ERR_GENERIC;

    if (a->user_agent && !nurl_headermap_has(m, "User-Agent")) {
        nurl_err_t err = nurl_headermap_set(m, "User-Agent", a->user_agent);
        if (err != NURL_OK) return err;
    }
    if (a->referer && !nurl_headermap_has(m, "Referer")) {
        nurl_err_t err = nurl_headermap_set(m, "Referer", a->referer);
        if (err != NURL_OK) return err;
    }
    if (a->cookie && a->cookie[0] != '@' && !nurl_headermap_has(m, "Cookie")) {
        nurl_err_t err = nurl_headermap_set(m, "Cookie", a->cookie);
        if (err != NURL_OK) return err;
    }

    return NURL_OK;
}

void nurl_request_from_args(NutRequest *req, const char *method, const char *url, const CommonArgs *a) {
    if (!req || !a) return;

    req->method = method;
    req->url = url;

    // Headers Map
    req->headers = nurl_headermap_new();
    if (req->headers) {
        for (size_t i = 0; i < a->header_count; i++) {
            nurl_headermap_add_raw(req->headers, a->header[i]);
        }

        if (a->upload_file || a->upload_fields_count > 0) {
            NutMultipart *m = nurl_multipart_new();
            if (m) {
                if (a->upload_file) {
                    nurl_multipart_add_file(m, a->upload_name ? a->upload_name : "file", a->upload_file, a->upload_mime);
                }
                for (size_t i = 0; i < a->upload_fields_count; i++) {
                    char *field = strdup(a->upload_fields[i]);
                    char *eq = strchr(field, '=');
                    if (eq) {
                        *eq = '\0';
                        nurl_multipart_add_field(m, field, eq + 1);
                    }
                    free(field);
                }
                nurl_multipart_into_request(m, req);
                nurl_multipart_free(m);
            }
        }

        nurl_headermap_apply_auth(req->headers, a);
        nurl_headermap_apply_common(req->headers, a);
        if (a->json && !nurl_headermap_has(req->headers, "Content-Type")) {
            nurl_headermap_set(req->headers, "Content-Type", "application/json");
        }
        if (a->compressed && !nurl_headermap_has(req->headers, "Accept-Encoding")) {
            nurl_headermap_set(req->headers, "Accept-Encoding", "gzip, deflate");
        }
    }

    req->body = (const uint8_t *)a->data;
    req->body_len = a->data_len;
    req->body_is_stream = false;

    req->read_timeout_sec = (unsigned int)a->timeout;
    req->connect_timeout_sec = (unsigned int)a->connect_timeout;
    req->follow_redirect = a->location;
    req->max_redirects = a->is_set.max_redirects ? a->max_redirects : 10;
    req->retry_count = a->retry;
    req->retry_delay_sec = a->retry_delay;
    req->limit_rate = a->limit_rate;
    req->fail_on_error = a->fail;
    req->fail_with_body = a->fail_with_body;

    req->tls_verify = !a->no_verify;
    req->cacert = a->cacert;
    req->cert = a->cert;
    req->key = a->key;

    if (a->tls12) {
        req->tls_version = 12;
    } else if (a->tls13) {
        req->tls_version = 13;
    } else {
        req->tls_version = 0;
    }

    req->proxy = a->proxy;
    req->proxy_user = a->proxy_user;
    req->no_proxy = a->no_proxy;
    req->connect_to = a->connect_to;

    req->cookie = a->cookie;
    req->cookie_jar = a->cookie_jar;
    req->session = a->session;

    req->include_headers = a->include;
    req->verbose = a->verbose;
    req->silent = a->silent;
    req->raw_output = a->raw;
    req->decompress = a->compressed;
    req->http10 = a->http10;

    req->resume = a->resume;
    req->progress = a->progress;
}

void nurl_request_free(NutRequest *req) {
    if (!req) return;
    if (req->headers) {
        nurl_headermap_free(req->headers);
    }
    if (req->body_parts) {
        for (size_t i = 0; i < req->body_parts_count; i++) {
            if (req->body_parts[i].type == NURL_BODY_PART_MEM) {
                free((void *)req->body_parts[i].data);
            } else if (req->body_parts[i].type == NURL_BODY_PART_FILE) {
                free((void *)req->body_parts[i].filepath);
            }
        }
        free(req->body_parts);
    }
    free(req);
}
