#pragma once
#include "config.h"

// Start the configuration/status web server (reachable from the AP side only).
void webui_start(const repeater_config_t *cfg);
