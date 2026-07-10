#ifndef NURL_HTTP_H
#define NURL_HTTP_H

#include "net/nurl_stream.h"
#include "engine/nurl_engine_types.h"
#include "errors/nurl_error.h"
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

/**
 * Sends a structured HTTP/1.1 request via the provided active buffered stream.
 * method: "GET", "POST", etc.
 * path: The request path and query parameters (e.g. "/json?q=1").
 * hostname: The destination Host header value.
 * extra_headers: Null-terminated string containing extra "Name: Value\r\n" headers, or NULL.
 * body: Payload bytes, or NULL.
 * body_len: Length of payload in bytes.
 * Returns NURL_OK on success, or an explicit nurl_err_t on failure.
 * If successful, *out_response will contain the dynamically allocated response.
 */

typedef struct NutHttpParams NutHttpParams;
typedef void (*nurl_headers_cb)(NutHttpParams *p, const nurl_http_response_t *res, void *user_data);

struct NutHttpParams {
    const char        *method;
    const char        *path;
    const char        *hostname;
    const char        *extra_headers;
    const uint8_t     *body;
    size_t             body_len;
    NutBodyPart      *body_parts;
    size_t             body_parts_count;
    FILE              *body_out;
    unsigned long      resume_offset;
    nurl_progress_cb   progress_cb;
    void              *progress_data;
    nurl_headers_cb    header_cb;
    void              *header_data;
    bool               http10;
};

nurl_err_t nurl_http_request(
    NutStream *stream,
    NutHttpParams *p,
    nurl_http_response_t **out_response
);

/**
 * Frees all memory associated with the response structure.
 */
void nurl_http_response_free(nurl_http_response_t *res);

#endif /* NURL_HTTP_H */
