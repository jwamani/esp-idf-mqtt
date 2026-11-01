#pragma once
#include <stdbool.h>

// connects wifi + mqtt to one topic
void net_start(const char *client_id, const char *subscribe_topic, const char *status_topic);

// sends a text message and does nothing if not connected
void net_publish(const char *topic, const char *msg);

// same as `net_publish` but the broker keeps the last message and sends it to new subscribers
void net_publish_retained(const char *topic, const char *msg);

// true once clock is set from the internet
bool net_time_ok(void);

// called on every message received
void on_message(const char *topic, const char *data);