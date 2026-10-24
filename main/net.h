#pragma once

// connects wifi + mqtt to one topic
void net_start(const char *client_id, const char *subscribe_topic);

// sends a text message and does nothing if not connected
void net_publish(const char *topic, const char *msg);

void on_message(const char *topic, const char *data);