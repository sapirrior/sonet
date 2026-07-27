#ifndef NPS_STREAM_H
#define NPS_STREAM_H

#include <stddef.h>
#include <stdbool.h>
#include "nps_tls.h"

#define NPS_STREAM_BUFFER_SIZE 8192

typedef struct {
    unsigned char data[NPS_STREAM_BUFFER_SIZE];
    size_t        pos;
    size_t        len;
} NpsStreamBuffer;

typedef struct NpsStream {
    int              fd;
    nps_tls_t      *tls;
    NpsStreamBuffer read_buf;
    unsigned long    limit_rate;    /* bytes per second, 0=unlimited */
    double           last_throttle_time;
    unsigned long    bytes_this_sec;
} NpsStream;

/**
 * Creates a new stream wrapping a socket and optionally a TLS context.
 * The stream does NOT take ownership of the fd or tls (it won't close them on free).
 */
NpsStream *nps_stream_new(int fd, nps_tls_t *tls);

/**
 * Sets the bandwidth limit rate in bytes per second.
 */
void        nps_stream_set_limit_rate(NpsStream *s, unsigned long rate);

/**
 * Frees the stream structure.
 */
void        nps_stream_free(NpsStream *s);

/**
 * Reads up to 'len' bytes into 'buf'. Returns bytes read, or <= 0 on error/EOF.
 * Uses the internal buffer to minimize syscalls/SSL_read calls.
 */
int         nps_stream_read(NpsStream *s, void *buf, size_t len);

/**
 * Reads a single line (up to \n) into 'buf' of max size 'max_len'.
 * The resulting string is null-terminated and includes the newline if space permits.
 * Returns bytes read (including \n), or <= 0 on error/EOF.
 */
int         nps_stream_read_line(NpsStream *s, char *buf, size_t max_len);

/**
 * Reads exactly 'len' bytes into 'buf'.
 * Returns 'len' on success, or <= 0 on error/EOF.
 */
int         nps_stream_read_exact(NpsStream *s, void *buf, size_t len);

/**
 * Writes 'len' bytes from 'buf' to the stream.
 * Returns bytes written, or <= 0 on error.
 */
int         nps_stream_write(NpsStream *s, const void *buf, size_t len);

/**
 * Checks if the stream has any buffered data remaining.
 */
bool        nps_stream_has_buffered(const NpsStream *s);

#endif /* NPS_STREAM_H */
