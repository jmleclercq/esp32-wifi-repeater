// WiFi repeater: connects to an upstream AP (STA) and re-exposes its own AP,
// routing traffic between the two with lwIP NAPT.
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "config.h"
#include "webui.h"

static const char *TAG = "repeater";

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

// Hand the upstream DNS server to AP clients, then enable NAPT.
static void on_sta_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    ESP_LOGW(TAG, "Upstream IP: " IPSTR " gw " IPSTR, IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
        ESP_LOGW(TAG, "Upstream channel %d, RSSI %d dBm", rec.primary, rec.rssi);
    }

    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
        dns.ip.u_addr.ip4.addr == 0) {
        esp_netif_set_ip4_addr(&dns.ip.u_addr.ip4, 1, 1, 1, 1);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
    }
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer = 1;  // OFFER_DNS
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(s_ap_netif);

    ESP_ERROR_CHECK(esp_netif_napt_enable(s_ap_netif));
    ESP_LOGW(TAG, "NAPT enabled");
}

static void on_sta_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const wifi_event_sta_disconnected_t *ev = data;
    ESP_LOGW(TAG, "Upstream lost (reason %d), reconnecting", ev->reason);
    esp_netif_napt_disable(s_ap_netif);
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_AP_STACONNECTED: {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "Client " MACSTR " joined", MAC2STR(e->mac));
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "Client " MACSTR " left", MAC2STR(e->mac));
        break;
    }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    // The AP must use a subnet different from the upstream one.
    esp_netif_ip_info_t ip = { 0 };
    IP4_ADDR(&ip.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip));
    esp_netif_dhcps_start(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_sta_disconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_sta_got_ip, NULL));

    repeater_config_t cfg_data;
    config_load(&cfg_data);
    if (config_validate(&cfg_data)) {
        // Stored/default settings unusable (e.g. weak AP password): keep the AP safe.
        ESP_LOGE(TAG, "Invalid settings: %s", config_validate(&cfg_data));
        if (strlen(cfg_data.ap_password) < 8) {
            strlcpy(cfg_data.ap_password, "changeme123", sizeof(cfg_data.ap_password));
        }
        if (strlen(cfg_data.admin_password) < 8) {
            strlcpy(cfg_data.admin_password, cfg_data.ap_password, sizeof(cfg_data.admin_password));
        }
    }

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, cfg_data.upstream_ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, cfg_data.upstream_password, sizeof(sta.sta.password));

    // Several APs may share the SSID: pick the strongest, not the first found.
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, cfg_data.ap_ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, cfg_data.ap_password, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(cfg_data.ap_ssid);
    ap.ap.max_connection = CONFIG_REPEATER_AP_MAX_CLIENTS;
    ap.ap.authmode = strlen(cfg_data.ap_password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    // Throughput: 20 MHz channels (40 MHz on 2.4 GHz forces 11n and suffers from
    // interference) -- must be set before start -- and no modem sleep.
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    const uint8_t proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX;
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, proto));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, proto));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    webui_start(&cfg_data);

}
