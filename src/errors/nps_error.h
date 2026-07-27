#ifndef NPS_ERROR_H
#define NPS_ERROR_H

typedef enum {
    NPS_OK            = 0,
    NPS_ERR_OOM       = 1,   /* malloc/realloc returned NULL */
    NPS_ERR_NETWORK   = 2,   /* TCP connect/recv/send failed */
    NPS_ERR_URL       = 4,   /* Malformed or unsupported URL */
    NPS_ERR_TLS       = 5,   /* TLS handshake or cert error */
    NPS_ERR_IO        = 6,   /* File read/write failed */
    NPS_ERR_RESOLVE   = 7,   /* Hostname resolution failed */
    NPS_ERR_CONNECT   = 8,   /* TCP connection failed */
    NPS_ERR_PROXY     = 9,   /* Proxy connection/handshake failed */
    NPS_ERR_TLS_HANDSHAKE = 10, /* Specific TLS handshake failure */
    NPS_ERR_TIMEOUT   = 28,  /* curl exit 28: operation timed out */
    NPS_ERR_HTTP_4XX  = 22,  /* curl exit 22: HTTP 4xx response with -f/--fail */
    NPS_ERR_HTTP_5XX  = 23,  /* mapped to 22 on exit */
    NPS_ERR_ARG       = 3,   /* curl exit 3: Bad CLI argument */
    NPS_ERR_GENERIC   = 99,
} nps_err_t;
#endif /* NPS_ERROR_H */
