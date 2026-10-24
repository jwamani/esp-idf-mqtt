#include <stdbool.h>
#include <stdio.h>
#include <time.h>

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
    net_start(HUB_CLIENT_ID, TOPIC_ROOMS_ALL, NULL); // no status topic for the hub, it is always online

    int open = 1;
    while (1) {
        net_publish_retained(TOPIC_HUB, open ? "open" : "closed"); // these messages are retained, so new subscribers will get the last message
        open = !open;

        if (net_time_ok()) {
            time_t now = time(NULL);
            struct tm t;
            localtime_r(&now, &t);
            ESP_LOGI(TAG, "time %02d:%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        }

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}