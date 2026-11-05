#pragma once
#include "esp_err.h"

typedef struct {
    char upstream_ssid[33];
    char upstream_password[65];
    char ap_ssid[33];
    char ap_password[65];
    char admin_password[65];  // web UI; defaults to the AP password until set
} repeater_config_t;

// Load settings from NVS, falling back to the Kconfig defaults.
void config_load(repeater_config_t *cfg);
esp_err_t config_save(const repeater_config_t *cfg);
// Erase all saved settings (next boot uses the Kconfig defaults).
esp_err_t config_reset(void);
// Returns NULL if valid, otherwise a human-readable error.
const char *config_validate(const repeater_config_t *cfg);
