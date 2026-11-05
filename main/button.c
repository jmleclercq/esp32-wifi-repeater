#include "button.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO GPIO_NUM_9
#define HOLD_MS 5000
#define POLL_MS 100

static const char *TAG = "button";

static void button_task(void *arg)
{
    int held = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            held += POLL_MS;
            if (held >= HOLD_MS) {
                ESP_LOGW(TAG, "BOOT held %d ms: erasing saved settings", HOLD_MS);
                config_reset();
                vTaskDelay(pdMS_TO_TICKS(300));
                esp_restart();
            }
        } else {
            held = 0;
        }
    }
}

void reset_button_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreate(button_task, "reset_button", 2048, NULL, 3, NULL);
}
