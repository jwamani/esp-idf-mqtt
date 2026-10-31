#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hub_config.h"
#include "hub_interfaces.h"
#include "occupancy.h"
#include "door_ctrl.h"
#include "rfid_reader.h"
#include "access_ctrl.h"

static const char *TAG = "access";

static QueueHandle_t s_scan_queue;

const char *access_result_text(access_result_t r)
{
    switch (r) {
    case ACCESS_GRANTED:        return "Granted";
    case ACCESS_EMERGENCY_EXIT: return "Emergency exit";
    case DENIED_UNKNOWN:        return "Unknown card";
    case DENIED_DISABLED:       return "Card disabled";
    case DENIED_OUTSIDE_HOURS:  return "Outside hours";
    case DENIED_ALREADY_INSIDE: return "Already inside";
    case DENIED_NOT_INSIDE:     return "Not inside";
    default:                    return "Error";
    }
}

/* The decision. The first check that fails ends it. */
static access_result_t decide(door_side_t side, const char *uid, user_t *user)
{
    bool known = user_lookup(uid, user);

    /* 0. Emergency override: at the exit, everyone may leave */
    if (side == SIDE_EXIT && override_active()) {
        return ACCESS_EMERGENCY_EXIT;
    }
    /* 1. Is the card in the user table? */
    if (!known) {
        return DENIED_UNKNOWN;
    }
    /* 2. Is the user switched on? */
    if (!user->enabled) {
        return DENIED_DISABLED;
    }
    /* 3. Is the building open? Applies to ENTRY and to EXIT (spec 2.6) */
    if (!schedule_is_open()) {
        return DENIED_OUTSIDE_HOURS;
    }
    /* 4. Anti-passback: you cannot enter twice or leave without entering */
    bool inside = occupancy_is_inside(uid);
    if (side == SIDE_ENTRY && inside) {
        return DENIED_ALREADY_INSIDE;
    }
    if (side == SIDE_EXIT && !inside) {
        return DENIED_NOT_INSIDE;
    }
    return ACCESS_GRANTED;
}

static void handle_scan(const scan_event_t *scan)
{
    user_t user;
    memset(&user, 0, sizeof user);
    strlcpy(user.name, "Unknown", sizeof user.name);

    access_result_t result = decide(scan->side, scan->uid, &user);

    switch (result) {
    case ACCESS_GRANTED:
        if (scan->side == SIDE_ENTRY) {
            if (!occupancy_add(scan->uid, user.name)) {
                ESP_LOGW(TAG, "inside list full or duplicate");
            }
            door_show_result(SIDE_ENTRY, true, "Welcome", user.name);
        } else {
            occupancy_remove(scan->uid);
            door_show_result(SIDE_EXIT, true, "Goodbye", user.name);
        }
        break;

    case ACCESS_EMERGENCY_EXIT:
        occupancy_remove(scan->uid);          /* harmless if not on the list */
        door_show_result(SIDE_EXIT, true, "EMERGENCY EXIT", "Please leave");
        break;

    default:
        door_show_result(scan->side, false, "ACCESS DENIED",
                         access_result_text(result));
        break;
    }

    ESP_LOGI(TAG, "%s %s (%s) -> %s, inside=%d",
             scan->side == SIDE_ENTRY ? "ENTRY" : "EXIT", scan->uid, user.name,
             access_result_text(result), occupancy_count());

    log_access(scan->side, scan->uid, user.name, result, occupancy_count());
}

/* The ONLY task that decides. One queue in, one decision at a time. */
static void access_task(void *arg)
{
    scan_event_t scan;
    while (1) {
        if (xQueueReceive(s_scan_queue, &scan, portMAX_DELAY) == pdTRUE) {
            handle_scan(&scan);
        }
    }
}

void access_submit_scan(door_side_t side, const char *uid)
{
    scan_event_t scan;
    scan.side = side;
    strlcpy(scan.uid, uid, sizeof scan.uid);
    xQueueSend(s_scan_queue, &scan, 0);
}

void access_init(void)
{
    occupancy_init();     /* must come before door_init: the screen reads it */
    door_init();
    s_scan_queue = xQueueCreate(8, sizeof(scan_event_t));
    xTaskCreate(access_task, "access_task", 4096, NULL, 5, NULL);
    rfid_init(s_scan_queue);    /* last: taps start flowing from here */
}