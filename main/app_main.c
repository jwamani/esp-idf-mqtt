#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "net.h"
#include "rooms.h"
#include "building.h"
#include "access_ctrl.h"
#include "storage.h"


void on_message(const char *topic, const char *data)
{
    rooms_handle_message(topic, data);
}

void app_main(void)
{
    net_start(HUB_CLIENT_ID, TOPIC_ROOMS_ALL, NULL); // no status topic for the hub, it is always online
    rooms_init();
    storage_init();  // SD card mount + users.csv into RAM (before RFID starts)
    access_init();   // occupancy list, doors, decision task, RFID readers

    while (1) {
        building_announce();                       // retained: open/closed/emergency + inside count
        // rooms_tick();                           // optional safety net: flag rooms that stopped reporting
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}
