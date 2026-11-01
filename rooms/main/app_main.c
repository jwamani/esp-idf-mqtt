// room_main.c - Room terminal (owner: rooms)
// light.c does the sensing and the lamp. This file talks to the hub.
#include <stdio.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "app_config.h"
#include "net.h"
#include "room_config.h"
#include "light.h"

static const char *TAG = "room";

#define LOOP_MS 500

// net.c calls this for every message from the hub (it runs inside the MQTT task, so keep it short)
void on_message(const char *topic, const char *data)
{
    ESP_LOGI(TAG, "building is %s", data);
    if (strcasecmp(data, "open") == 0) {
        light_set_building_open(true);
    } else if (strcasecmp(data, "closed") == 0) {
        light_set_building_open(false);
    } else if (strcasecmp(data, "emergency") == 0) {
        light_set_building_open(true);   // exits unlocked; the room behaves as open
    }
}

void app_main(void)
{
    light_start();   // first, so the light works even if the network is down

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    int room = mac[5];   // last byte of the MAC is the room number (set in diagram.json)

    // net.c keeps pointers to these, so they are static
    static char client_id[32];
    static char online_topic[64];
    static char status_topic[64];
    snprintf(client_id, sizeof(client_id), ROOM_CLIENT_PREFIX "%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(online_topic, sizeof(online_topic), "cocis-g1/rooms/%02x%02x%02x/online", mac[3], mac[4], mac[5]);
    snprintf(status_topic, sizeof(status_topic), "cocis-g1/rooms/%02x%02x%02x/status", mac[3], mac[4], mac[5]);

    net_start(client_id, TOPIC_HUB, online_topic);   // waits here until WiFi is up

    char msg[128];
    uint32_t seen_alerts = light_intrusion_count();
    bool last_occ = false, last_lit = false;
    int ticks = 0;

    while (1) {
        bool occ = light_is_occupied();
        bool lit = light_is_on();

        // status: when something changes, and every STATUS_PERIOD_MS as a heartbeat
        if (occ != last_occ || lit != last_lit || ticks >= STATUS_PERIOD_MS / LOOP_MS) {
            uint32_t on_s = light_on_seconds();
            uint32_t open_s = light_open_seconds();
            int saved = open_s ? 100 - (int)((100UL * on_s) / open_s) : 0;   // % of open time the lamp was off
            snprintf(msg, sizeof(msg), "room=%d occupied=%d light=%d on_s=%u open_s=%u saved_pct=%d",
                     room, occ, lit, (unsigned)on_s, (unsigned)open_s, saved);
            net_publish(status_topic, msg);
            last_occ = occ;
            last_lit = lit;
            ticks = 0;
        }

        // alert: light.c counts intrusions, we send one message for each new one
        uint32_t alerts = light_intrusion_count();
        if (alerts != seen_alerts) {
            seen_alerts = alerts;
            snprintf(msg, sizeof(msg), "room=%d INTRUSION", room);
            net_publish(TOPIC_ROOM_ALERT, msg);
        }

        ticks++;
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}