#include "nps_decompress.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

unsigned char *nps_decompress_gzip_deflate(const unsigned char *src, size_t src_len, size_t *out_len) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // 15 + 32 enables zlib and gzip decoding with automatic header detection
    if (inflateInit2(&strm, 15 + 32) != Z_OK) {
        return NULL;
    }

    strm.next_in = (Bytef *)src;
    strm.avail_in = src_len;

    size_t dest_cap = src_len * 2 + 1024;
    unsigned char *dest = malloc(dest_cap + 1); /* +1 for null terminator */
    if (!dest) {
        inflateEnd(&strm);
        return NULL;
    }

    strm.next_out = dest;
    strm.avail_out = dest_cap;

    while (strm.avail_in > 0) {
        int ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            free(dest);
            inflateEnd(&strm);
            return NULL;
        }
        if (strm.avail_out == 0) {
            size_t bytes_written = dest_cap - strm.avail_out;
            dest_cap *= 2;
            unsigned char *temp = realloc(dest, dest_cap + 1); /* +1 for null terminator */
            if (!temp) {
                free(dest);
                inflateEnd(&strm);
                return NULL;
            }
            dest = temp;
            strm.next_out = dest + bytes_written;
            strm.avail_out = (uInt)(dest_cap - bytes_written);
        }
    }

    size_t written = dest_cap - strm.avail_out;
    dest[written] = '\0';
    *out_len = written;

    inflateEnd(&strm);
    return dest;
}
