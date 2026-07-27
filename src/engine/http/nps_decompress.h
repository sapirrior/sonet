#ifndef NPS_DECOMPRESS_H
#define NPS_DECOMPRESS_H

#include <stddef.h>

/**
 * Decompresses a gzip or deflate compressed buffer.
 *
 * @param src The compressed source buffer.
 * @param src_len The length of the compressed source buffer.
 * @param out_len Pointer to a variable where the decompressed length will be written.
 * @return Dynamically allocated decompressed buffer (null-terminated), or NULL on failure.
 *         The caller is responsible for freeing the returned pointer.
 */
unsigned char *nps_decompress_gzip_deflate(const unsigned char *src, size_t src_len, size_t *out_len);

#endif /* NPS_DECOMPRESS_H */
