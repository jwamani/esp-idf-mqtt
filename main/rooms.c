#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rooms.h"
#include "storage.h"

static const char *TAG = "rooms";

/* a room is offline if its status is older than this (status is sent every 5 s).
   Only used when rooms_tick() is called from the main loop. */
#define ROOM_ALIVE_TIMEOUT_MS 40000

static room_state_t      s_rooms[MAX_ROOMS];
static int               s_count = 0;
static SemaphoreHandle_t s_lock;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* topic looks like  cocis-g1/rooms/<id>/<what>  */
static void parse_topic(const char *topic, char *id, char *what)
{
    const char *p = strstr(topic, "/rooms/");
    if (!p) {
        return;
    }
    p += 7;                             /* skip the /rooms/ part */

    int i = 0;
    while (p[i] && p[i] != '/') {
        i++;
    }
    snprintf(id, 8, "%.*s", i, p);

    if (p[i] == '/') {                  /* there is a /<what> after the id */
        snprintf(what, 16, "%s", p + i + 1);
    } else {
        what[0] = '\0';                 /* no type: message for the table itself */
    }
}

/* find a row for this id, or add a new one */
static room_state_t *find_or_add(const char *id)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_rooms[i].id, id) == 0) {
            return &s_rooms[i];
        }
    }
    if (s_count < MAX_ROOMS) {
        room_state_t *r = &s_rooms[s_count++];
        memset(r, 0, sizeof *r);
        strlcpy(r->id, id, sizeof r->id);
        return r;
    }
    return NULL;
}

// this is called from the MQTT callback, it will store the room state in the table, and log any alerts
static void handle_event(const char *id, const char *what, const char *data)
{
    bool log_this = false;
    char logged_what[16];

    xSemaphoreTake(s_lock, portMAX_DELAY);
    room_state_t *r = find_or_add(id);

    if (r) {
        r->last_seen = now_ms();
        if (!what[0] || strcmp(what, "online") == 0) {
            size_t      len = strlen(data);     /* "id online" vs "id offline" */
            bool is_online = (len < 7 || strcmp(data + len - 7, "offline") != 0);
            if (is_online != r->online_logged) {
                r->online_logged = is_online;
                log_this = true;
                strlcpy(logged_what, "online", sizeof logged_what);
            }
            r->online = is_online;
            ESP_LOGI(TAG, "room %s %s", r->id, r->online ? "online" : "offline");
        }
        else if (strcmp(what, "status") == 0) {
            /* supports both "occupied=1 light=0" and the richer
               "room=1 occupied=1 light=0 on_s=..." payload from the rooms */
            const char *o = strstr(data, "occupied=");
            const char *l = strstr(data, "light=");
            int occ = (o ? atoi(o + 9) : 0);
            int light = (l ? atoi(l + 6) : 0);
            bool new_occ = (occ != 0), new_light = (light != 0);
            /* log only what changed, so the 5 s repeat messages stay off the card */
            if (new_occ  != r->occ_logged || new_light != r->light_logged) {
                r->occ_logged   = new_occ;
                r->light_logged = new_light;
                log_this = true;
                strlcpy(logged_what, what, sizeof logged_what);
            }
            r->occupied = new_occ;
            r->light    = new_light;
        }
        else if (strcmp(what, "alert") == 0) {
            ESP_LOGW(TAG, "ALERT from room %s: %s", r->id, data);
            log_this = true;
            strlcpy(logged_what, what, sizeof logged_what);
        }
    }
    xSemaphoreGive(s_lock);

    /* the SD write happens here, after the lock: storage.c owns the card */
    if (log_this) {
        log_room_event(id, logged_what, data);
    }
}

void rooms_handle_message(const char *topic, const char *data)
{
    char id[8] = "";
    char what[16] = "";
    parse_topic(topic, id, what);

    if (id[0] == '\0') {                /* topic was not a room topic */
        ESP_LOGW(TAG, "unknown topic %s", topic);
        return;
    }
    if (strcmp(id, "alert") == 0) {     /* shared alert topic: cocis-g1/rooms/alert */
        ESP_LOGW(TAG, "ALERT: %s", data);
        log_room_event("unknown", "alert", data);
        return;
    }
    handle_event(id, what, data);
}

void rooms_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

void rooms_tick(void)
{
    int64_t t = now_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        if (s_rooms[i].online && t - s_rooms[i].last_seen > ROOM_ALIVE_TIMEOUT_MS) {
            s_rooms[i].online = false;
            ESP_LOGW(TAG, "room %s went quiet, marked offline", s_rooms[i].id);
        }
    }
    xSemaphoreGive(s_lock);
}

int rooms_list(room_state_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = (s_count < max) ? s_count : max;
    memcpy(out, s_rooms, (size_t)n * sizeof(room_state_t));
    xSemaphoreGive(s_lock);
    return n;
}
