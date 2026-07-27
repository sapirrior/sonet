#ifndef NPS_ENGINE_REQUEST_H
#define NPS_ENGINE_REQUEST_H

#include "nps.h"
#include "engine/utils/nps_headers.h"
#include "engine/nps_engine_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct NpsConnPool NpsConnPool;

typedef struct NpsRequest NpsRequest;
typedef void (*nps_req_headers_cb)(NpsRequest *req, const nps_http_response_t *res, void *user_data);

struct NpsRequest {
    const char      *method;       /* "GET", "POST", etc. */
    const char      *url;
    NpsHeaderMap  *headers;
    const uint8_t   *body;
    size_t           body_len;
    bool             body_is_stream; /* read from stdin lazily */

    NpsBodyPart    *body_parts;
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
    nps_progress_cb progress_cb;
    void            *progress_data;
    nps_req_headers_cb header_cb;
    void            *header_data;

    NpsConnPool *pool;
    struct NpsStream *stream;
    char             last_tls_error[256];
};

NpsRequest *nps_request_new(void);
void         nps_request_from_args(NpsRequest *req, const char *method,
                                    const char *url, const CommonArgs *a);
void         nps_request_free(NpsRequest *req);

/* Internal request building helpers */
nps_err_t nps_headermap_apply_auth(NpsHeaderMap *m, const CommonArgs *a);
nps_err_t nps_headermap_apply_common(NpsHeaderMap *m, const CommonArgs *a);

#endif /* NPS_ENGINE_REQUEST_H */
