#include "nurl_multipart.h"
#include "utils/nurl_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char *name;
    char *value;
    char *filepath;
    char *mime;
    bool is_file;
} MultipartPart;

struct NutMultipart {
    char boundary[64];
    MultipartPart *parts;
    size_t count;
    char *cached_content_type;
};

NutMultipart *nurl_multipart_new(void) {
    NutMultipart *m = calloc(1, sizeof(NutMultipart));
    if (!m) return NULL;

    /* Generate a boundary with enough entropy: combine time, pid, and random */
    unsigned long r1 = (unsigned long)time(NULL);
    unsigned long r2;
#if defined(_WIN32)
    r2 = (unsigned long)GetTickCount();
#elif defined(__linux__)
    /* Use /dev/urandom for better randomness */
    FILE *rnd = fopen("/dev/urandom", "rb");
    if (rnd) {
        if (fread(&r2, sizeof(r2), 1, rnd) != 1) r2 = (unsigned long)rand();
        fclose(rnd);
    } else {
        r2 = (unsigned long)rand();
    }
#else
    r2 = (unsigned long)arc4random();
#endif
    snprintf(m->boundary, sizeof(m->boundary), "------------------------%08lx%08lx", r1, r2);

    return m;
}

void nurl_multipart_add_file(NutMultipart *m, const char *field_name, const char *filepath, const char *mime_type) {
    MultipartPart *temp = realloc(m->parts, sizeof(MultipartPart) * (m->count + 1));
    if (!temp) return; /* OOM: skip this part silently */
    m->parts = temp;
    MultipartPart *p = &m->parts[m->count];
    p->name = strdup(field_name);
    p->value = NULL;
    p->filepath = strdup(filepath);
    p->mime = mime_type ? strdup(mime_type) : strdup("application/octet-stream");
    if (!p->name || !p->filepath || !p->mime) {
        /* Partial OOM: clean up what was allocated */
        free(p->name); free(p->filepath); free(p->mime);
        return;
    }
    p->is_file = true;
    m->count++;
}

void nurl_multipart_add_field(NutMultipart *m, const char *name, const char *value) {
    MultipartPart *temp = realloc(m->parts, sizeof(MultipartPart) * (m->count + 1));
    if (!temp) return; /* OOM: skip this part silently */
    m->parts = temp;
    MultipartPart *p = &m->parts[m->count];
    p->name = strdup(name);
    p->value = strdup(value);
    p->filepath = NULL;
    p->mime = NULL;
    if (!p->name || !p->value) {
        free(p->name); free(p->value);
        return;
    }
    p->is_file = false;
    m->count++;
}

const char *nurl_multipart_content_type(const NutMultipart *m) {
    if (((NutMultipart *)m)->cached_content_type) free(((NutMultipart *)m)->cached_content_type);
    char buf[128];
    snprintf(buf, sizeof(buf), "multipart/form-data; boundary=%s", m->boundary);
    ((NutMultipart *)m)->cached_content_type = strdup(buf);
    return m->cached_content_type;
}

void nurl_multipart_into_request(NutMultipart *m, NutRequest *req) {
    if (!m || !req) return;

    /*
     * Worst-case allocation: each part generates 3 body parts
     * (header MEM, body MEM/FILE, trailing CRLF MEM) plus 1 final boundary.
     */
    size_t max_parts = 3 * m->count + 1;
    req->body_parts = calloc(max_parts, sizeof(NutBodyPart));
    if (!req->body_parts) return;
    req->body_parts_count = 0;

    for (size_t i = 0; i < m->count; i++) {
        MultipartPart *p = &m->parts[i];
        char header[512];
        if (p->is_file) {
            const char *filename = strrchr(p->filepath, '/');
            filename = filename ? filename + 1 : p->filepath;
            snprintf(header, sizeof(header), "--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                     m->boundary, p->name, filename, p->mime);
        } else {
            snprintf(header, sizeof(header), "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n",
                     m->boundary, p->name);
        }

        /* Header part (MEM) */
        uint8_t *hdr_copy = (uint8_t *)strdup(header);
        if (!hdr_copy) goto oom_cleanup;
        req->body_parts[req->body_parts_count].type = NURL_BODY_PART_MEM;
        req->body_parts[req->body_parts_count].data = hdr_copy;
        req->body_parts[req->body_parts_count].len = strlen(header);
        req->body_parts_count++;

        /* Body part */
        if (p->is_file) {
            const char *fp = strdup(p->filepath);
            if (!fp) goto oom_cleanup;
            req->body_parts[req->body_parts_count].type = NURL_BODY_PART_FILE;
            req->body_parts[req->body_parts_count].filepath = fp;
            req->body_parts[req->body_parts_count].len = 0; /* determined at send time */
        } else {
            uint8_t *val_copy = (uint8_t *)strdup(p->value);
            if (!val_copy) goto oom_cleanup;
            req->body_parts[req->body_parts_count].type = NURL_BODY_PART_MEM;
            req->body_parts[req->body_parts_count].data = val_copy;
            req->body_parts[req->body_parts_count].len = strlen(p->value);
        }
        req->body_parts_count++;

        /* Trailing CRLF after body part */
        uint8_t *crlf = (uint8_t *)strdup("\r\n");
        if (!crlf) goto oom_cleanup;
        req->body_parts[req->body_parts_count].type = NURL_BODY_PART_MEM;
        req->body_parts[req->body_parts_count].data = crlf;
        req->body_parts[req->body_parts_count].len = 2;
        req->body_parts_count++;
    }

    /* Final boundary */
    char final[128];
    snprintf(final, sizeof(final), "--%s--\r\n", m->boundary);
    uint8_t *fin_copy = (uint8_t *)strdup(final);
    if (!fin_copy) goto oom_cleanup;
    req->body_parts[req->body_parts_count].type = NURL_BODY_PART_MEM;
    req->body_parts[req->body_parts_count].data = fin_copy;
    req->body_parts[req->body_parts_count].len = strlen(final);
    req->body_parts_count++;

    nurl_headermap_set(req->headers, "Content-Type", nurl_multipart_content_type(m));
    return;

oom_cleanup:
    /* Free any already-allocated parts and signal failure */
    for (size_t j = 0; j < req->body_parts_count; j++) {
        if (req->body_parts[j].type == NURL_BODY_PART_MEM) {
            free((void *)req->body_parts[j].data);
        } else if (req->body_parts[j].type == NURL_BODY_PART_FILE) {
            free((void *)req->body_parts[j].filepath);
        }
    }
    free(req->body_parts);
    req->body_parts = NULL;
    req->body_parts_count = 0;
}

void nurl_multipart_free(NutMultipart *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++) {
        free(m->parts[i].name);
        free(m->parts[i].value);
        free(m->parts[i].filepath);
        free(m->parts[i].mime);
    }
    free(m->parts);
    free(m->cached_content_type);
    free(m);
}
