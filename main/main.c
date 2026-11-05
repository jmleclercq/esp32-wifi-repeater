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

static const char *TAG = "repeater";

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

// Hand the upstream DNS server to AP clients, then enable NAPT.
static void on_sta_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "Upstream IP: " IPSTR " gw " IPSTR, IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));

    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
        dns.ip.u_addr.ip4.addr == 0) {
        IP_ADDR4(&dns.ip, 1, 1, 1, 1);
    }
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer = 1;  // OFFER_DNS
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(s_ap_netif);

    ESP_ERROR_CHECK(esp_netif_napt_enable(s_ap_netif));
    ESP_LOGI(TAG, "NAPT enabled");
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

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, CONFIG_REPEATER_UPSTREAM_SSID, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, CONFIG_REPEATER_UPSTREAM_PASSWORD, sizeof(sta.sta.password));

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, CONFIG_REPEATER_AP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, CONFIG_REPEATER_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(CONFIG_REPEATER_AP_SSID);
    ap.ap.max_connection = CONFIG_REPEATER_AP_MAX_CLIENTS;
    ap.ap.authmode = strlen(CONFIG_REPEATER_AP_PASSWORD) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
}
