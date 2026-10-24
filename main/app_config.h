#pragma once

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define MQTT_BROKER_URI "mqtt://192.168.1.2:1883"
#define MQTT_TOPIC "demo/chat"

#define PUBLISH_PERIOD_MS 5000

// topics
#define TOPIC_ROOM_STATUS_FILTER "cocis-g1/rooms/+/status" // rooms -> hub
#define TOPIC_HUB "cocis-g1/building" // hub -> rooms
#define TOPIC_ROOM_ALERT "cocis-g1/rooms/alert" // rooms -> hub
#define TOPIC_ROOM_ONLINE_FILTER "cocis-g1/rooms/+/online" // rooms -> hub
#define TOPIC_ROOMS_ALL "cocis-g1/rooms/#" // hub subscribes to this

#define ROOM_CLIENT_PREFIX "cocis-g1-room-"
#define HUB_CLIENT_ID "cocis-g1-hub"

#define NTP_SERVER "pool.ntp.org"
#define TIMEZONE "EAT-3" // EAT <=> UTC+3

