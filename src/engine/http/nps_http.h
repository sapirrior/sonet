#ifndef NPS_HTTP_H
#define NPS_HTTP_H

#include "net/nps_stream.h"
#include "engine/nps_engine_types.h"
#include "errors/nps_error.h"
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
 * Returns NPS_OK on success, or an explicit nps_err_t on failure.
 * If successful, *out_response will contain the dynamically allocated response.
 */

typedef struct NpsHttpParams NpsHttpParams;
typedef void (*nps_headers_cb)(NpsHttpParams *p, const nps_http_response_t *res, void *user_data);

struct NpsHttpParams {
    const char        *method;
    const char        *path;
    const char        *hostname;
    const char        *extra_headers;
    const uint8_t     *body;
    size_t             body_len;
    NpsBodyPart      *body_parts;
    size_t             body_parts_count;
    FILE              *body_out;
    unsigned long      resume_offset;
    nps_progress_cb   progress_cb;
    void              *progress_data;
    nps_headers_cb    header_cb;
    void              *header_data;
    bool               http10;
};

nps_err_t nps_http_request(
    NpsStream *stream,
    NpsHttpParams *p,
    nps_http_response_t **out_response
);

/**
 * Frees all memory associated with the response structure.
 */
void nps_http_response_free(nps_http_response_t *res);

#endif /* NPS_HTTP_H */
