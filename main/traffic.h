#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_netif.h"

#define TRAFFIC_MAX_CLIENTS 10

typedef struct {
    uint8_t mac[6];
    uint64_t to_client;    // bytes sent to this client (its download)
    uint64_t from_client;  // bytes received from this client (its upload)
} traffic_client_t;

typedef struct {
    uint64_t to_clients;
    uint64_t from_clients;
    int num_clients;
    traffic_client_t clients[TRAFFIC_MAX_CLIENTS];
} traffic_snapshot_t;

// Hook the AP interface to count bytes per client MAC. Call after the AP has started.
void traffic_attach(esp_netif_t *ap_netif);
void traffic_client_connected(const uint8_t mac[6]);
void traffic_client_disconnected(const uint8_t mac[6]);
void traffic_get(traffic_snapshot_t *out);
