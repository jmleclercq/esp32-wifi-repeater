// Minimal configuration + status page.
// - Only reachable from the repeater's own subnet (192.168.4.0/24).
// - HTTP Basic auth: user "admin", separate admin password (defaults to the AP password).
#include "webui.h"
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "traffic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

static const char *TAG = "webui";
static repeater_config_t s_cfg;

static bool from_ap_subnet(httpd_req_t *req)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&ss, &len) != 0 || ss.ss_family != AF_INET) {
        return false;
    }
    uint32_t a = ((struct sockaddr_in *)&ss)->sin_addr.s_addr;  // network order, read little-endian
    return (a & 0x00FFFFFF) == 0x0004A8C0;                       // 192.168.4.x
}

static bool authorized(httpd_req_t *req)
{
    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK ||
        strncmp(hdr, "Basic ", 6) != 0) {
        return false;
    }
    unsigned char dec[128];
    size_t n = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &n, (unsigned char *)hdr + 6, strlen(hdr + 6)) != 0) {
        return false;
    }
    dec[n] = 0;
    char expect[96];
    snprintf(expect, sizeof(expect), "admin:%s", s_cfg.admin_password);
    size_t el = strlen(expect);
    if (n != el) {
        return false;
    }
    unsigned char diff = 0;  // constant-time compare
    for (size_t i = 0; i < el; i++) {
        diff |= dec[i] ^ (unsigned char)expect[i];
    }
    return diff == 0;
}

// Returns true if the request may proceed; otherwise sends the error response.
static bool gate(httpd_req_t *req)
{
    if (!from_ap_subnet(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Only available from the repeater network");
        return false;
    }
    if (!authorized(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"WiFi Repeater\"");
        httpd_resp_sendstr(req, "Authentication required (user: admin)");
        return false;
    }
    return true;
}

static void send_escaped(httpd_req_t *req, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<': httpd_resp_sendstr_chunk(req, "&lt;"); break;
        case '>': httpd_resp_sendstr_chunk(req, "&gt;"); break;
        case '&': httpd_resp_sendstr_chunk(req, "&amp;"); break;
        case '"': httpd_resp_sendstr_chunk(req, "&quot;"); break;
        case '\'': httpd_resp_sendstr_chunk(req, "&#39;"); break;
        default: httpd_resp_send_chunk(req, s, 1);
        }
    }
}

// Text field, pre-filled with the current value.
static void send_text(httpd_req_t *req, const char *label, const char *name, const char *val)
{
    httpd_resp_sendstr_chunk(req, "<label>");
    httpd_resp_sendstr_chunk(req, label);
    httpd_resp_sendstr_chunk(req, "<input name=\"");
    httpd_resp_sendstr_chunk(req, name);
    httpd_resp_sendstr_chunk(req, "\" maxlength=\"32\" value=\"");
    send_escaped(req, val);
    httpd_resp_sendstr_chunk(req, "\"></label>");
}

// Password field: never echoes the stored secret; blank means "keep current".
static void send_password(httpd_req_t *req, const char *label, const char *name)
{
    httpd_resp_sendstr_chunk(req, "<label>");
    httpd_resp_sendstr_chunk(req, label);
    httpd_resp_sendstr_chunk(req, "<input name=\"");
    httpd_resp_sendstr_chunk(req, name);
    httpd_resp_sendstr_chunk(req, "\" type=\"password\" maxlength=\"63\" autocomplete=\"new-password\" "
                                  "placeholder=\"leave blank to keep current\"></label>");
}

static const char *PAGE_HEAD =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>WiFi Repeater</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:30rem;margin:1rem auto;padding:0 1rem;color:#222}"
    "h1{font-size:1.4rem}fieldset{margin:1rem 0;border:1px solid #ccc;border-radius:6px}"
    "label{display:block;margin:.6rem 0}input{display:block;width:100%;padding:.4rem;box-sizing:border-box}"
    "button{padding:.6rem 1.2rem}table td,table th{padding:.15rem .8rem .15rem 0;text-align:left;white-space:nowrap}.err{color:#b00020}.ok{color:#0a7d2c}"
    "</style></head><body><h1>WiFi Repeater</h1>";

static void fmt_bytes(char *out, size_t n, uint64_t b)
{
    if (b >= 1024ULL * 1024 * 1024) {
        snprintf(out, n, "%.2f GB", b / 1073741824.0);
    } else if (b >= 1024 * 1024) {
        snprintf(out, n, "%.1f MB", b / 1048576.0);
    } else {
        snprintf(out, n, "%.0f KB", b / 1024.0);
    }
}

// Status block (also served alone on /status for the live refresh).
static void send_status(httpd_req_t *req)
{
    char buf[200], a[24], b[24];
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    wifi_ap_record_t rec;
    httpd_resp_sendstr_chunk(req, "<h2>Status</h2><table>");
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK && sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr) {
        snprintf(buf, sizeof(buf), "<tr><td>Upstream</td><td class=\"ok\">connected, channel %d, %d dBm</td></tr>", rec.primary, rec.rssi);
        httpd_resp_sendstr_chunk(req, buf);
        snprintf(buf, sizeof(buf), "<tr><td>Upstream IP</td><td>" IPSTR "</td></tr>", IP2STR(&ip.ip));
        httpd_resp_sendstr_chunk(req, buf);
    } else {
        httpd_resp_sendstr_chunk(req, "<tr><td>Upstream</td><td class=\"err\">not connected</td></tr>");
    }
    snprintf(buf, sizeof(buf), "<tr><td>Firmware</td><td>%s</td></tr>", esp_app_get_description()->version);
    httpd_resp_sendstr_chunk(req, buf);
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(buf, sizeof(buf), "<tr><td>Uptime</td><td>%lud %02luh %02lum</td></tr>", (unsigned long)(up / 86400),
             (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
    httpd_resp_sendstr_chunk(req, buf);

    traffic_snapshot_t t;
    traffic_get(&t);
    fmt_bytes(a, sizeof(a), t.to_clients);
    fmt_bytes(b, sizeof(b), t.from_clients);
    snprintf(buf, sizeof(buf), "<tr><td>Traffic</td><td>&darr; %s &nbsp; &uarr; %s <small>(since boot)</small></td></tr>", a, b);
    httpd_resp_sendstr_chunk(req, buf);
    snprintf(buf, sizeof(buf), "<tr><td>Free heap</td><td>%lu KB</td></tr></table>", (unsigned long)(esp_get_free_heap_size() / 1024));
    httpd_resp_sendstr_chunk(req, buf);

    wifi_sta_list_t list;
    wifi_sta_mac_ip_list_t ips;
    memset(&ips, 0, sizeof(ips));
    int clients = 0;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
        clients = list.num;
        esp_wifi_ap_get_sta_list_with_ip(&list, &ips);
    }
    snprintf(buf, sizeof(buf), "<h2>Clients (%d)</h2>", clients);
    httpd_resp_sendstr_chunk(req, buf);
    if (clients == 0) {
        httpd_resp_sendstr_chunk(req, "<p>No client connected.</p>");
        return;
    }
    httpd_resp_sendstr_chunk(req, "<table class=\"clients\"><tr><th>IP</th><th>MAC</th><th>Signal</th><th>&darr;</th><th>&uarr;</th></tr>");
    for (int i = 0; i < clients; i++) {
        const uint8_t *mac = list.sta[i].mac;
        char ipstr[16] = "-";
        for (int k = 0; k < ips.num; k++) {
            if (memcmp(ips.sta[k].mac, mac, 6) == 0 && ips.sta[k].ip.addr) {
                snprintf(ipstr, sizeof(ipstr), IPSTR, IP2STR(&ips.sta[k].ip));
            }
        }
        a[0] = b[0] = '-'; a[1] = b[1] = 0;
        for (int k = 0; k < t.num_clients; k++) {
            if (memcmp(t.clients[k].mac, mac, 6) == 0) {
                fmt_bytes(a, sizeof(a), t.clients[k].to_client);
                fmt_bytes(b, sizeof(b), t.clients[k].from_client);
            }
        }
        snprintf(buf, sizeof(buf), "<tr><td>%s</td><td>" MACSTR "</td><td>%d dBm</td><td>%s</td><td>%s</td></tr>", ipstr,
                 MAC2STR(mac), list.sta[i].rssi, a, b);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "</table>");
}

static esp_err_t get_status(httpd_req_t *req)
{
    if (!gate(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    send_status(req);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t get_root(httpd_req_t *req)
{
    if (!gate(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<div id=\"status\">");
    send_status(req);
    httpd_resp_sendstr_chunk(req, "</div>");

    httpd_resp_sendstr_chunk(req, "<h2>Settings</h2><form method=\"post\" action=\"/save\">"
                                  "<fieldset><legend>Upstream network (your router, 2.4 GHz)</legend>");
    send_text(req, "SSID", "up_ssid", s_cfg.upstream_ssid);
    send_password(req, "Password", "up_pass");
    httpd_resp_sendstr_chunk(req, "<label><input type=\"checkbox\" name=\"up_open\" style=\"display:inline;width:auto\"> "
                                  "Open network (no password)</label>");
    httpd_resp_sendstr_chunk(req, "</fieldset><fieldset><legend>Repeater network</legend>");
    send_text(req, "SSID", "ap_ssid", s_cfg.ap_ssid);
    send_password(req, "Password (8+ characters)", "ap_pass");
    httpd_resp_sendstr_chunk(req, "</fieldset><fieldset><legend>This page</legend>");
    send_password(req, "Admin password (user: admin, 8+ characters)", "adm_pass");
    httpd_resp_sendstr_chunk(req, "</fieldset><button>Save and restart</button></form>"
                                  "<script>setInterval(function(){fetch('/status').then(function(r){return r.ok?r.text():null})"
                                  ".then(function(t){if(t)document.getElementById('status').innerHTML=t})},5000)</script>"
                                  "</body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static bool form_field(const char *body, const char *key, char *dst, size_t size)
{
    char tmp[200];
    if (httpd_query_key_value(body, key, tmp, sizeof(tmp)) != ESP_OK) {
        return false;
    }
    url_decode(tmp);
    if (strlen(tmp) >= size) {
        return false;
    }
    strlcpy(dst, tmp, size);
    return true;
}

// Overwrite dst only if the field is present and non-empty.
static bool optional_field(const char *body, const char *key, char *dst, size_t size)
{
    char tmp[200];
    if (httpd_query_key_value(body, key, tmp, sizeof(tmp)) != ESP_OK) {
        return true;
    }
    url_decode(tmp);
    if (tmp[0] == 0) {
        return true;
    }
    if (strlen(tmp) >= size) {
        return false;
    }
    strlcpy(dst, tmp, size);
    return true;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t post_save(httpd_req_t *req)
{
    if (!gate(req)) {
        return ESP_OK;
    }
    if (req->content_len == 0 || req->content_len > 1200) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request size");
    }
    char body[1201];  // 4 fields, URL-encoded (worst case ~3x)
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive error");
        }
        got += r;
    }
    body[got] = 0;

    repeater_config_t n = s_cfg;  // start from current values; blank password fields keep them
    char dummy[2];
    if (!form_field(body, "up_ssid", n.upstream_ssid, sizeof(n.upstream_ssid)) ||
        !form_field(body, "ap_ssid", n.ap_ssid, sizeof(n.ap_ssid)) ||
        !optional_field(body, "up_pass", n.upstream_password, sizeof(n.upstream_password)) ||
        !optional_field(body, "ap_pass", n.ap_password, sizeof(n.ap_password)) ||
        !optional_field(body, "adm_pass", n.admin_password, sizeof(n.admin_password))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or too long field");
    }
    if (httpd_query_key_value(body, "up_open", dummy, sizeof(dummy)) != ESP_ERR_NOT_FOUND) {
        n.upstream_password[0] = 0;  // checkbox ticked: open upstream network
    }
    const char *bad = config_validate(&n);
    bool saved = false;
    if (!bad) {
        saved = (config_save(&n) == ESP_OK);
    }
    ESP_LOGW(TAG, "Settings %s", bad ? "rejected" : (saved ? "saved, restarting" : "NOT saved (flash error)"));
    httpd_resp_set_type(req, "text/html");
    if (saved) {
        httpd_resp_set_hdr(req, "Refresh", "15;url=/");  // re-prompts for login after reconnect
    }
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    if (bad) {
        httpd_resp_sendstr_chunk(req, "<p class=\"err\">");
        httpd_resp_sendstr_chunk(req, bad);
        httpd_resp_sendstr_chunk(req, "</p><p><a href=\"/\">Back</a></p></body></html>");
    } else if (!saved) {
        httpd_resp_sendstr_chunk(req, "<p class=\"err\">Could not save to flash.</p><p><a href=\"/\">Back</a></p></body></html>");
    } else {
        httpd_resp_sendstr_chunk(req, "<p class=\"ok\">Saved. The repeater is restarting; reconnect to the repeater WiFi "
                                      "(with the new SSID/password if you changed them). "
                                      "This page reloads in 15 seconds.</p></body></html>");
        xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

void webui_start(const repeater_config_t *cfg)
{
    s_cfg = *cfg;
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = 8192;
    hc.lru_purge_enable = true;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &hc) != ESP_OK) {
        ESP_LOGE(TAG, "Could not start web server");
        return;
    }
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = get_root };
    const httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = post_save };
    httpd_register_uri_handler(srv, &root);
    const httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = get_status };
    httpd_register_uri_handler(srv, &save);
    httpd_register_uri_handler(srv, &status);
    ESP_LOGW(TAG, "Web UI on http://192.168.4.1/");
}
