#ifndef NPS_ENGINE_TYPES_H
#define NPS_ENGINE_TYPES_H

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
} nps_http_response_t;

/* Multipart/Body parts */
typedef enum {
    NPS_BODY_PART_MEM,
    NPS_BODY_PART_FILE
} NpsBodyPartType;

typedef struct {
    NpsBodyPartType type;
    const uint8_t *data;
    size_t len;
    const char *filepath;
} NpsBodyPart;

/* Progress callback */
typedef void (*nps_progress_cb)(unsigned long downloaded, unsigned long total, bool finished, void *user_data);

#endif /* NPS_ENGINE_TYPES_H */
