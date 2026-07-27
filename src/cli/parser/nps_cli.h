#ifndef NPS_CLI_H
#define NPS_CLI_H

#include "nps.h"

/**
 * Initializes CommonArgs with default values.
 */
void nps_cli_init_args(CommonArgs *args);

/**
 * Parses the CLI arguments into CommonArgs, routing command name and target URL.
 * Returns 0 on success, non-zero on parsing error or help request.
 */
int nps_cli_parse(int argc, char **argv, CommonArgs *args, char **url);

/**
 * Frees all dynamically allocated strings/arrays within CommonArgs.
 */
void nps_cli_free_args(CommonArgs *args);

#endif /* NPS_CLI_H */
