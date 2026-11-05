#include "config.h"
#include <string.h>
#include "nvs.h"
#include "sdkconfig.h"

#define NS "repeater"

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t size, const char *def)
{
    size_t len = size;
    if (!h || nvs_get_str(h, key, dst, &len) != ESP_OK) {
        strlcpy(dst, def, size);
    }
}

void config_load(repeater_config_t *cfg)
{
    nvs_handle_t h = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        h = 0;
    }
    load_str(h, "up_ssid", cfg->upstream_ssid, sizeof(cfg->upstream_ssid), CONFIG_REPEATER_UPSTREAM_SSID);
    load_str(h, "up_pass", cfg->upstream_password, sizeof(cfg->upstream_password), CONFIG_REPEATER_UPSTREAM_PASSWORD);
    load_str(h, "ap_ssid", cfg->ap_ssid, sizeof(cfg->ap_ssid), CONFIG_REPEATER_AP_SSID);
    load_str(h, "ap_pass", cfg->ap_password, sizeof(cfg->ap_password), CONFIG_REPEATER_AP_PASSWORD);
    // Until an admin password is set, fall back to the AP password.
    load_str(h, "adm_pass", cfg->admin_password, sizeof(cfg->admin_password), cfg->ap_password);
    if (h) {
        nvs_close(h);
    }
}

esp_err_t config_save(const repeater_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = nvs_set_str(h, "up_ssid", cfg->upstream_ssid)) == ESP_OK &&
        (err = nvs_set_str(h, "up_pass", cfg->upstream_password)) == ESP_OK &&
        (err = nvs_set_str(h, "ap_ssid", cfg->ap_ssid)) == ESP_OK &&
        (err = nvs_set_str(h, "ap_pass", cfg->ap_password)) == ESP_OK &&
        (err = nvs_set_str(h, "adm_pass", cfg->admin_password)) == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

const char *config_validate(const repeater_config_t *cfg)
{
    size_t n;
    if ((n = strlen(cfg->upstream_ssid)) < 1 || n > 32) return "Upstream SSID must be 1-32 characters";
    if ((n = strlen(cfg->upstream_password)) != 0 && (n < 8 || n > 63)) return "Upstream password must be empty (open network) or 8-63 characters";
    if ((n = strlen(cfg->ap_ssid)) < 1 || n > 32) return "Repeater SSID must be 1-32 characters";
    if ((n = strlen(cfg->ap_password)) < 8 || n > 63) return "Repeater password must be 8-63 characters";
    if ((n = strlen(cfg->admin_password)) < 8 || n > 63) return "Admin password must be 8-63 characters";
    return NULL;
}
