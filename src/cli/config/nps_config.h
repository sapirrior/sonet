#ifndef NPS_CONFIG_H
#define NPS_CONFIG_H

#include "nps.h"

/**
 * Loads the config file (from NPS_CONFIG env var or ~/.config/nps/config.toml)
 * and merges the defaults and custom headers into the parsed CommonArgs.
 */
void nps_config_load_and_merge(CommonArgs *args);

#endif /* NPS_CONFIG_H */
