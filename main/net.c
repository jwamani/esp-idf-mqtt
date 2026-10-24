#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"

#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "net.h"
#include "app_config.h"

static const char *TAG = "net";

static EventGroupHandle_t wifi_events;
static esp_mqtt_client_handle_t mqtt_client;
static const char *my_client_id;
static const char *my_topic;
static const char *my_status_topic;

static char online_msg[64];
static char offline_msg[64];
static volatile bool mqtt_connected;

#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "Wifi disconnected, retrying");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Wifi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());                // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Create default event loop
    esp_netif_create_default_wifi_sta();              // Create default Wi-Fi station

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));                                                         // Initialize Wi-Fi
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));  // Register Wi-Fi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL)); // Register IP event handler

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_OPEN,
        }};

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));               // Set Wi-Fi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // Set Wi-Fi configuration
    ESP_ERROR_CHECK(esp_wifi_start());                               // Start Wi-Fi

    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY); // wait for the wifi connection
}

// Start the clock (NTP)
static void clock_start(void) {
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config)); // sets clock in the background
}

// MQTT event handler
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT connected as %s", my_client_id);
        esp_mqtt_client_subscribe(event->client, my_topic, 1); // qos 1 (atleast once).
        if (my_status_topic) {
            esp_mqtt_client_publish(event->client, my_status_topic, online_msg, 0, 1, 1); // qos 1 (atleast once), retain
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
    {
        char topic[64];
        char text[128];
        snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);
        snprintf(text, sizeof(text), "%.*s", event->data_len, event->data);
        on_message(topic, text);
        break;
    }
    default:
        break;
    }
}

// Start the network (Wi-Fi + MQTT)
void net_start(const char *client_id, const char *subscribe_topic, const char *status_topic)
{
    my_client_id = client_id;
    my_topic = subscribe_topic;
    my_status_topic = status_topic;
    snprintf(online_msg, sizeof(online_msg), "%s online", client_id);
    snprintf(offline_msg, sizeof(offline_msg), "%s offline", client_id);

    ESP_ERROR_CHECK(nvs_flash_init()); // Initialize NVS for storing Wi-Fi credentials
    wifi_start();
    clock_start();

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = my_client_id
    };

    // last will; broker will send this message if the client disconnects unexpectedly. who receives this message? an
    if (status_topic) {
        mqtt_config.session.last_will.topic = status_topic;
        mqtt_config.session.last_will.msg = offline_msg;
        mqtt_config.session.last_will.msg_len = strlen(offline_msg);
        mqtt_config.session.last_will.qos = 1;
        mqtt_config.session.last_will.retain = 1;
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

// to be used for 
void net_publish(const char *topic, const char *msg) {
    if (mqtt_connected) {
        esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 0, 0);
    }
}

// to be used for status messages, the broker will keep the last message and send it to new subscribers
void net_publish_retained(const char *topic, const char *msg) {
    if (mqtt_connected) {
        esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 1);
    }
}

bool net_time_ok(void) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return (timeinfo.tm_year > (2020 - 1900)); // year is since 1900
}