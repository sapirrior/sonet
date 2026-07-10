#ifndef NURL_PROGRESS_H
#define NURL_PROGRESS_H

#include "engine/nurl_engine_request.h"
#include <stdbool.h>

typedef struct {
    unsigned long resume_offset;
    double start_time;
    double last_update;
    bool silent;
} NutProgressCtx;

/**
 * The actual callback implementation that draws to the terminal.
 */
void nurl_progress_update(unsigned long downloaded, unsigned long total, bool finished, void *user_data);

#endif /* NURL_PROGRESS_H */
