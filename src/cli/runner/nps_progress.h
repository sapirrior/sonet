#ifndef NPS_PROGRESS_H
#define NPS_PROGRESS_H

#include "engine/nps_engine_request.h"
#include <stdbool.h>

typedef struct {
    unsigned long resume_offset;
    double start_time;
    double last_update;
    bool silent;
} NpsProgressCtx;

/**
 * The actual callback implementation that draws to the terminal.
 */
void nps_progress_update(unsigned long downloaded, unsigned long total, bool finished, void *user_data);

#endif /* NPS_PROGRESS_H */
