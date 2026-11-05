idf_component_register(SRCS "main.c" "config.c" "webui.c" INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_http_server esp_wifi esp_netif esp_event nvs_flash mbedtls lwip)
