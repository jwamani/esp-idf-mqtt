#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "app_config.h"
#include "net.h"

static const char *TAG = "MAIN_HUB";

void on_message(const char *topic, const char *data)
{
    ESP_LOGI(TAG, "RX %s: %s", topic, data);
}

void app_main(void)
{
    net_start(HUB_CLIENT_ID, TOPIC_ROOMS);

    int open = 1;
    while (1) {
        net_publish(TOPIC_HUB, open ? "open" : "closed");
        open = !open;
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}