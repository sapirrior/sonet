#ifndef NURL_ENGINE_TYPES_H
#define NURL_ENGINE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* HTTP Response structure */
typedef struct {
    int status_code;
    char *status_text;
    char **headers;
    size_t header_count;
    unsigned char *body;
    size_t body_len;
} nurl_http_response_t;

/* Multipart/Body parts */
typedef enum {
    NURL_BODY_PART_MEM,
    NURL_BODY_PART_FILE
} NutBodyPartType;

typedef struct {
    NutBodyPartType type;
    const uint8_t *data;
    size_t len;
    const char *filepath;
} NutBodyPart;

/* Progress callback */
typedef void (*nurl_progress_cb)(unsigned long downloaded, unsigned long total, bool finished, void *user_data);

#endif /* NURL_ENGINE_TYPES_H */
