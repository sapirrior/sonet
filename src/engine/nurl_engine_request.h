#ifndef NURL_ENGINE_REQUEST_H
#define NURL_ENGINE_REQUEST_H

#include "nurl.h"
#include "engine/utils/nurl_headers.h"
#include "engine/nurl_engine_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct NutConnPool NutConnPool;

typedef struct NutRequest NutRequest;
typedef void (*nurl_req_headers_cb)(NutRequest *req, const nurl_http_response_t *res, void *user_data);

struct NutRequest {
    const char      *method;       /* "GET", "POST", etc. */
    const char      *url;
    NutHeaderMap  *headers;
    const uint8_t   *body;
    size_t           body_len;
    bool             body_is_stream; /* read from stdin lazily */

    NutBodyPart    *body_parts;
    size_t           body_parts_count;

    /* Transfer config */
    unsigned int     read_timeout_sec;
    unsigned int     connect_timeout_sec;
    bool             follow_redirect;
    unsigned int     max_redirects;   /* default: 10 */
    unsigned int     retry_count;
    unsigned int     retry_delay_sec;
    unsigned long    limit_rate;
    bool             fail_on_error;
    bool             fail_with_body;

    /* TLS config */
    bool             tls_verify;
    const char      *cacert;
    const char      *cert;
    const char      *key;
    int              tls_version;     /* 0=auto, 12=TLSv1.2, 13=TLSv1.3 */

    /* Proxy */
    const char      *proxy;
    const char      *proxy_user;
    const char      *no_proxy;
    const char      *connect_to;

    /* Cookies */
    const char      *cookie;
    const char      *cookie_jar;
    const char      *session;

    /* Output */
    FILE            *out;             /* stdout or file handle */
    bool             include_headers; /* print response headers */
    bool             verbose;
    bool             silent;
    bool             raw_output;
    bool             decompress;
    bool             http10;

    /* Download-specific */
    bool             resume;
    bool             progress;
    unsigned long    resume_offset;
    nurl_progress_cb progress_cb;
    void            *progress_data;
    nurl_req_headers_cb header_cb;
    void            *header_data;

    NutConnPool *pool;
    struct NutStream *stream;
    char             last_tls_error[256];
};

NutRequest *nurl_request_new(void);
void         nurl_request_from_args(NutRequest *req, const char *method,
                                    const char *url, const CommonArgs *a);
void         nurl_request_free(NutRequest *req);

/* Internal request building helpers */
nurl_err_t nurl_headermap_apply_auth(NutHeaderMap *m, const CommonArgs *a);
nurl_err_t nurl_headermap_apply_common(NutHeaderMap *m, const CommonArgs *a);

#endif /* NURL_ENGINE_REQUEST_H */
