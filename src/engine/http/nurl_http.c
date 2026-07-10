#include "nurl_http.h"
#include "nurl_buf.h"
#include "errors/nurl_diag.h"
#include "compat/nurl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

nurl_err_t nurl_http_request(
    NutStream *stream,
    NutHttpParams *p,
    nurl_http_response_t **out_response
) {
    if (out_response) *out_response = NULL;

    size_t total_body_len = p->body_len;
    if (p->body_parts && p->body_parts_count > 0) {
        total_body_len = 0;
        for (size_t i = 0; i < p->body_parts_count; i++) {
            if (p->body_parts[i].type == NURL_BODY_PART_MEM) {
                total_body_len += p->body_parts[i].len;
            } else if (p->body_parts[i].type == NURL_BODY_PART_FILE) {
                struct stat st;
                if (nurl_stat(p->body_parts[i].filepath, &st) == 0) {
                    total_body_len += st.st_size;
                }
            }
        }
    }

    // 1. Construct HTTP request headers
    NutBuf req_buf;
    nurl_buf_init(&req_buf);

    bool has_user_agent = false;
    bool has_connection = false;
    if (p->extra_headers) {
        const char *eh = p->extra_headers;
        while ((eh = nurl_strcasestr(eh, "User-Agent")) != NULL) {
            if (eh == p->extra_headers || eh[-1] == '\n' || eh[-1] == '\r') {
                const char *colon = strchr(eh, ':');
                if (colon) {
                    has_user_agent = true;
                    break;
                }
            }
            eh++;
        }
        eh = p->extra_headers;
        while ((eh = nurl_strcasestr(eh, "Connection")) != NULL) {
            if (eh == p->extra_headers || eh[-1] == '\n' || eh[-1] == '\r') {
                const char *colon = strchr(eh, ':');
                if (colon) {
                    has_connection = true;
                    break;
                }
            }
            eh++;
        }
    }

    if (!nurl_buf_printf(&req_buf, "%s %s HTTP/%s\r\nHost: %s\r\n", p->method, p->path, p->http10 ? "1.0" : "1.1", p->hostname)) {
        nurl_buf_free(&req_buf);
        return NURL_ERR_OOM;
    }

    if (!has_user_agent) {
        if (!nurl_buf_printf(&req_buf, "User-Agent: nurl/" NURL_VERSION "\r\n")) {
            nurl_buf_free(&req_buf);
            return NURL_ERR_OOM;
        }
    }
    if (!has_connection) {
        if (!nurl_buf_printf(&req_buf, "Connection: %s\r\n", p->http10 ? "close" : "keep-alive")) {
            nurl_buf_free(&req_buf);
            return NURL_ERR_OOM;
        }
    }

    if (total_body_len > 0) {
        if (!nurl_buf_printf(&req_buf, "Content-Length: %zu\r\n", total_body_len)) {
            nurl_buf_free(&req_buf);
            return NURL_ERR_OOM;
        }
    }

    if (p->extra_headers) {
        if (!nurl_buf_append(&req_buf, p->extra_headers, strlen(p->extra_headers))) {
            nurl_buf_free(&req_buf);
            return NURL_ERR_OOM;
        }
    }

    // Append final CRLF separating headers and body
    if (!nurl_buf_append(&req_buf, "\r\n", 2)) {
        nurl_buf_free(&req_buf);
        return NURL_ERR_OOM;
    }

    // Send HTTP Headers
    int w_headers = nurl_stream_write(stream, req_buf.data, req_buf.len);
    if (w_headers <= 0) {
        nurl_err_t err = (w_headers < 0) ? (nurl_err_t)(-w_headers) : NURL_ERR_NETWORK;
        nurl_buf_free(&req_buf);
        return err;
    }
    nurl_buf_free(&req_buf);

    // Send HTTP Body Parts if present
    if (p->body_parts && p->body_parts_count > 0) {
        for (size_t i = 0; i < p->body_parts_count; i++) {
            NutBodyPart *part = &p->body_parts[i];
            if (part->type == NURL_BODY_PART_MEM) {
                if (part->data && part->len > 0) {
                    int wn = nurl_stream_write(stream, part->data, part->len);
                    if (wn <= 0) {
                        return (wn < 0) ? (nurl_err_t)(-wn) : NURL_ERR_NETWORK;
                    }
                }
            } else if (part->type == NURL_BODY_PART_FILE) {
                if (part->filepath) {
                    FILE *bf = fopen(part->filepath, "rb");
                    if (!bf) {
                        nurl_diag_err("could not open '%s' for reading: %s", part->filepath, strerror(errno));
                        return NURL_ERR_IO;
                    }
                    char send_buf[65536];
                    size_t r;
                    while ((r = fread(send_buf, 1, sizeof(send_buf), bf)) > 0) {
                        int wn = nurl_stream_write(stream, send_buf, r);
                        if (wn <= 0) {
                            nurl_err_t err = (wn < 0) ? (nurl_err_t)(-wn) : NURL_ERR_NETWORK;
                            fclose(bf);
                            return err;
                        }
                    }
                    fclose(bf);
                }
            }
        }
    } else if (p->body && p->body_len > 0) {
        int wn = nurl_stream_write(stream, p->body, p->body_len);
        if (wn <= 0) {
            return (wn < 0) ? (nurl_err_t)(-wn) : NURL_ERR_NETWORK;
        }
    }

    // 2. Read response headers
    nurl_http_response_t *res = calloc(1, sizeof(nurl_http_response_t));
    if (!res) return NURL_ERR_OOM;

    char line_buf[8192];
    int line_len;

    // Read Status Line
    line_len = nurl_stream_read_line(stream, line_buf, sizeof(line_buf));
    if (line_len <= 0) {
        nurl_err_t err = (line_len < 0) ? (nurl_err_t)(-line_len) : NURL_ERR_NETWORK;
        nurl_http_response_free(res);
        return err;
    }

    // Parse status line: e.g. "HTTP/1.1 200 OK"
    char *version = strchr(line_buf, ' ');
    if (version) {
        version++;
        char *code_end = NULL;
        long status_code_l = strtol(version, &code_end, 10);
        res->status_code = (code_end != version && status_code_l >= 100 && status_code_l <= 999)
                           ? (int)status_code_l : 0;
        char *status_text_start = code_end && *code_end == ' ' ? code_end + 1 : NULL;
        if (status_text_start) {
            // Trim \r\n
            char *ptr = status_text_start;
            while (*ptr && *ptr != '\r' && *ptr != '\n') ptr++;
            *ptr = '\0';
            res->status_text = strdup(status_text_start);
        } else {
            res->status_text = strdup("Unknown");
        }
    } else {
        res->status_code = 0;
        res->status_text = strdup("Unknown");
    }

    if (!res->status_text) {
        nurl_http_response_free(res);
        return NURL_ERR_OOM;
    }

    // Read headers until empty line
    bool is_chunked = false;
    size_t content_len = 0;
    unsigned long total_len = 0;

    while (1) {
        line_len = nurl_stream_read_line(stream, line_buf, sizeof(line_buf));
        if (line_len <= 0) {
            if (line_len < 0) {
                nurl_http_response_free(res);
                return (nurl_err_t)(-line_len);
            }
            break; // Premature EOF
        }
        if (line_len <= 2 && (line_buf[0] == '\n' || (line_buf[0] == '\r' && line_buf[1] == '\n'))) {
            break; // End of headers
        }

        // Trim trailing CRLF for strdup
        char *ptr = line_buf + line_len - 1;
        while (ptr >= line_buf && (*ptr == '\r' || *ptr == '\n')) {
            *ptr = '\0';
            ptr--;
        }

        char **temp = realloc(res->headers, sizeof(char *) * (res->header_count + 1));
        if (!temp) {
            nurl_http_response_free(res);
            return NURL_ERR_OOM;
        }
        res->headers = temp;
        res->headers[res->header_count] = strdup(line_buf);
        if (!res->headers[res->header_count]) {
            nurl_http_response_free(res);
            return NURL_ERR_OOM;
        }
        res->header_count++;

        // Detect chunked or Content-Length
        char *colon = strchr(line_buf, ':');
        if (colon) {
            *colon = '\0';
            char *key = line_buf;
            char *val = colon + 1;
            while (isspace((unsigned char)*val)) val++;
            if (nurl_strcasecmp(key, "Transfer-Encoding") == 0 && nurl_strcasecmp(val, "chunked") == 0) {
                is_chunked = true;
            } else if (nurl_strcasecmp(key, "Content-Length") == 0) {
                content_len = strtoul(val, NULL, 10);
            } else if (nurl_strcasecmp(key, "Content-Range") == 0) {
                char *slash = strchr(val, '/');
                if (slash) {
                    total_len = strtoul(slash + 1, NULL, 10);
                }
            }
        }
    }

    if (p->header_cb) {
        p->header_cb(p, res, p->header_data);
    }

    if (nurl_strcasecmp(p->method, "HEAD") == 0) {
        if (out_response) *out_response = res;
        return NURL_OK;
    }

    unsigned long total_len_computed = total_len;
    bool is_resume = (res->status_code == 206 && p->resume_offset > 0);
    if (total_len_computed == 0) {
        total_len_computed = is_resume ? (content_len + p->resume_offset) : content_len;
    }

    unsigned long downloaded = p->resume_offset;

    // 3. Read body from socket (either streaming to body_out or accumulating in memory)
    if (is_chunked) {
        size_t body_cap = 8192;
        size_t body_len = 0;
        unsigned char *body_buf = NULL;
        if (!p->body_out) {
            body_buf = malloc(body_cap + 1); /* +1 for null terminator */
            if (!body_buf) {
                nurl_http_response_free(res);
                return NURL_ERR_OOM;
            }
        }

        while (1) {
            char chunk_size_buf[128];
            int size_line_len = nurl_stream_read_line(stream, chunk_size_buf, sizeof(chunk_size_buf));
            if (size_line_len <= 0) {
                if (size_line_len < 0) {
                    if (body_buf) free(body_buf);
                    nurl_http_response_free(res);
                    return (nurl_err_t)(-size_line_len);
                }
                break; // Premature EOF
            }

            unsigned long chunk_size = strtoul(chunk_size_buf, NULL, 16);
            if (chunk_size == 0) {
                // Consume trailing CRLF of the last chunk size 0
                char dummy[2];
                int dr = nurl_stream_read_exact(stream, dummy, 2);
                if (dr <= 0) {
                    if (body_buf) free(body_buf);
                    nurl_http_response_free(res);
                    return (dr < 0) ? (nurl_err_t)(-dr) : NURL_ERR_NETWORK;
                }
                break; // Finished
            }

            size_t read_chunk = 0;
            while (read_chunk < chunk_size) {
                char chunk_buf[4096];
                size_t to_read = chunk_size - read_chunk;
                if (to_read > sizeof(chunk_buf)) to_read = sizeof(chunk_buf);
                int n = nurl_stream_read(stream, chunk_buf, to_read);
                if (n <= 0) {
                    nurl_err_t err = (n < 0) ? (nurl_err_t)(-n) : NURL_ERR_NETWORK;
                    if (body_buf) free(body_buf);
                    nurl_http_response_free(res);
                    return err;
                }

                if (p->body_out) {
                    fwrite(chunk_buf, 1, n, p->body_out);
                } else {
                    if (body_len + n + 1 >= body_cap) {
                        body_cap = (body_cap + n + 1) * 2;
                        unsigned char *temp = realloc(body_buf, body_cap + 1);
                        if (!temp) {
                            free(body_buf);
                            nurl_http_response_free(res);
                            return NURL_ERR_OOM;
                        }
                        body_buf = temp;
                    }
                    memcpy(body_buf + body_len, chunk_buf, n);
                }
                body_len += n;
                read_chunk += n;
                downloaded += n;
                if (p->progress_cb) p->progress_cb(downloaded, total_len_computed, false, p->progress_data);
            }

            // Consume trailing CRLF of this chunk
            char dummy[2];
            int dr = nurl_stream_read_exact(stream, dummy, 2);
            if (dr <= 0) {
                if (body_buf) free(body_buf);
                nurl_http_response_free(res);
                return (dr < 0) ? (nurl_err_t)(-dr) : NURL_ERR_NETWORK;
            }
        }

        if (!p->body_out && body_buf) {
            body_buf[body_len] = '\0';
            res->body = body_buf;
            res->body_len = body_len;
        }
    } else if (content_len > 0) {
        size_t body_len = 0;
        unsigned char *body_buf = NULL;
        if (!p->body_out) {
            body_buf = malloc(content_len + 1); /* +1 for null terminator */
            if (!body_buf) {
                nurl_http_response_free(res);
                return NURL_ERR_OOM;
            }
        }

        while (body_len < content_len) {
            char chunk_buf[4096];
            size_t to_read = content_len - body_len;
            if (to_read > sizeof(chunk_buf)) to_read = sizeof(chunk_buf);
            int n = nurl_stream_read(stream, chunk_buf, to_read);
            if (n <= 0) {
                if (body_buf) free(body_buf);
                nurl_http_response_free(res);
                return (n < 0) ? (nurl_err_t)(-n) : NURL_ERR_NETWORK;
            }

            if (p->body_out) {
                fwrite(chunk_buf, 1, n, p->body_out);
            } else {
                memcpy(body_buf + body_len, chunk_buf, n);
            }
            body_len += n;
            downloaded += n;
            if (p->progress_cb) p->progress_cb(downloaded, total_len_computed, false, p->progress_data);
        }

        if (!p->body_out && body_buf) {
            body_buf[body_len] = '\0';
            res->body = body_buf;
            res->body_len = body_len;
        }
    } else {
        // Read until EOF
        size_t body_cap = 8192;
        size_t body_len = 0;
        unsigned char *body_buf = NULL;
        if (!p->body_out) {
            body_buf = malloc(body_cap + 1); /* +1 for null terminator */
            if (!body_buf) {
                nurl_http_response_free(res);
                return NURL_ERR_OOM;
            }
        }

        char chunk_buf[4096];
        int n;
        while ((n = nurl_stream_read(stream, chunk_buf, sizeof(chunk_buf))) != 0) {
            if (n < 0) {
                if (body_buf) free(body_buf);
                nurl_http_response_free(res);
                return (nurl_err_t)(-n);
            }
            if (p->body_out) {
                fwrite(chunk_buf, 1, n, p->body_out);
            } else {
                if (body_len + n + 1 >= body_cap) {
                    body_cap = (body_cap + n + 1) * 2;
                    unsigned char *temp = realloc(body_buf, body_cap + 1);
                    if (!temp) {
                        free(body_buf);
                        nurl_http_response_free(res);
                        return NURL_ERR_OOM;
                    }
                    body_buf = temp;
                }
                memcpy(body_buf + body_len, chunk_buf, n);
            }
            body_len += n;
            downloaded += n;
            if (p->progress_cb) p->progress_cb(downloaded, total_len_computed, false, p->progress_data);
        }

        if (!p->body_out && body_buf) {
            body_buf[body_len] = '\0';
            res->body = body_buf;
            res->body_len = body_len;
        }
    }

    if (p->progress_cb) p->progress_cb(downloaded, total_len_computed, true, p->progress_data);

    if (out_response) *out_response = res;
    return NURL_OK;
}

void nurl_http_response_free(nurl_http_response_t *res) {
    if (res) {
        if (res->status_text) {
            free(res->status_text);
        }
        if (res->headers) {
            for (size_t i = 0; i < res->header_count; i++) {
                free(res->headers[i]);
            }
            free(res->headers);
        }
        if (res->body) {
            free(res->body);
        }
        free(res);
    }
}
