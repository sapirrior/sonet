#ifndef NPS_UTILS_H
#define NPS_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Parses a URL into its constituent components.
 * Returns 0 on success, non-zero on failure.
 * The caller must free *scheme, *host, and *path if they are non-NULL.
 */
int nps_utils_parse_url(const char *url, char **scheme, char **host, int *port, char **path);

/**
 * Returns a redacted version of header values if the header is sensitive.
 * Returns "[hidden]" for Authorization and Cookie headers, or the original value otherwise.
 */
const char *nps_utils_redact_header(const char *key, const char *value);

/**
 * Trim leading and trailing whitespace from a string.
 * Returns a pointer to the trimmed substring within the original buffer (modifies the buffer).
 */
char *nps_utils_trim(char *str);

/**
 * Encodes a buffer of bytes to a dynamically allocated base64 string.
 * The caller is responsible for freeing the returned pointer.
 */
char *nps_utils_base64_encode(const unsigned char *src, size_t len);

char *nps_utils_read_stdin(size_t *out_len);

/**
 * Returns the current epoch time in seconds (high resolution).
 */
double nps_utils_get_time_sec(void);

#endif /* NPS_UTILS_H */
