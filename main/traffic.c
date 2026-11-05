// Per-client traffic accounting.
//
// The AP netif handles complete Ethernet frames, so wrapping its input() and
// linkoutput() hooks lets us attribute bytes to a client by MAC address:
//   - input():      source MAC (offset 6)      = client uploading
//   - linkoutput(): destination MAC (offset 0) = client downloading
// Counters are 32-bit atomics bumped per packet and folded into 64-bit totals
// under a mutex (periodically and on read), so 32-bit wraparound is harmless.
#include "traffic.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

static const char *TAG = "traffic";

typedef struct {
    uint8_t mac[6];
    atomic_bool used;
    atomic_uint tx_pending, rx_pending;  // to / from client, since last fold
    uint64_t tx_total, rx_total;         // guarded by s_lock
} slot_t;

static slot_t s_slots[TRAFFIC_MAX_CLIENTS];
static atomic_uint s_tx_pending, s_rx_pending;
static uint64_t s_tx_total, s_rx_total;
static SemaphoreHandle_t s_lock;
static netif_input_fn s_orig_input;
static netif_linkoutput_fn s_orig_output;
static bool s_attached;

static slot_t *find_slot(const uint8_t *mac)
{
    for (int i = 0; i < TRAFFIC_MAX_CLIENTS; i++) {
        if (atomic_load(&s_slots[i].used) && memcmp(s_slots[i].mac, mac, 6) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static err_t ap_input(struct pbuf *p, struct netif *netif)
{
    unsigned len = p->tot_len;
    atomic_fetch_add(&s_rx_pending, len);
    if (p->len >= 14) {
        slot_t *s = find_slot((const uint8_t *)p->payload + 6);
        if (s) {
            atomic_fetch_add(&s->rx_pending, len);
        }
    }
    return s_orig_input(p, netif);
}

static err_t ap_output(struct netif *netif, struct pbuf *p)
{
    unsigned len = p->tot_len;
    atomic_fetch_add(&s_tx_pending, len);
    if (p->len >= 14) {
        slot_t *s = find_slot((const uint8_t *)p->payload);
        if (s) {
            atomic_fetch_add(&s->tx_pending, len);
        }
    }
    return s_orig_output(netif, p);
}

static void fold_locked(void)
{
    s_tx_total += atomic_exchange(&s_tx_pending, 0);
    s_rx_total += atomic_exchange(&s_rx_pending, 0);
    for (int i = 0; i < TRAFFIC_MAX_CLIENTS; i++) {
        s_slots[i].tx_total += atomic_exchange(&s_slots[i].tx_pending, 0);
        s_slots[i].rx_total += atomic_exchange(&s_slots[i].rx_pending, 0);
    }
}

static void fold_timer_cb(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fold_locked();
    xSemaphoreGive(s_lock);
}

void traffic_attach(esp_netif_t *ap_netif)
{
    struct netif *n = esp_netif_get_netif_impl(ap_netif);
    if (!n || s_attached) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    s_orig_input = n->input;
    s_orig_output = n->linkoutput;
    if (!s_orig_input || !s_orig_output) {
        ESP_LOGE(TAG, "AP netif not ready, traffic counters disabled");
        return;
    }
    n->input = ap_input;
    n->linkoutput = ap_output;
    s_attached = true;

    // Fold well before a 32-bit counter could wrap (~4 GB).
    const esp_timer_create_args_t args = { .callback = fold_timer_cb, .name = "traffic_fold" };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_periodic(t, 20 * 1000 * 1000);
    }
}

void traffic_client_connected(const uint8_t mac[6])
{
    if (!s_attached || find_slot(mac)) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < TRAFFIC_MAX_CLIENTS; i++) {
        if (!atomic_load(&s_slots[i].used)) {
            memcpy(s_slots[i].mac, mac, 6);
            s_slots[i].tx_total = s_slots[i].rx_total = 0;
            atomic_store(&s_slots[i].tx_pending, 0);
            atomic_store(&s_slots[i].rx_pending, 0);
            atomic_store(&s_slots[i].used, true);  // last: makes the slot visible
            break;
        }
    }
    xSemaphoreGive(s_lock);
}

void traffic_client_disconnected(const uint8_t mac[6])
{
    slot_t *s = s_attached ? find_slot(mac) : NULL;
    if (s) {
        atomic_store(&s->used, false);
    }
}

void traffic_get(traffic_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_attached) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fold_locked();
    out->to_clients = s_tx_total;
    out->from_clients = s_rx_total;
    for (int i = 0; i < TRAFFIC_MAX_CLIENTS; i++) {
        if (atomic_load(&s_slots[i].used)) {
            traffic_client_t *c = &out->clients[out->num_clients++];
            memcpy(c->mac, s_slots[i].mac, 6);
            c->to_client = s_slots[i].tx_total;
            c->from_client = s_slots[i].rx_total;
        }
    }
    xSemaphoreGive(s_lock);
}
