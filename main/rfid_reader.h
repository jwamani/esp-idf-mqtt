#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Start both readers. Each accepted tap is sent to scan_queue
   as a scan_event_t (side + card UID text). */
void rfid_init(QueueHandle_t scan_queue);