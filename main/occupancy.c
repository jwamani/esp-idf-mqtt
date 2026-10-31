#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hub_config.h"
#include "occupancy.h"

static occupant_t        s_list[MAX_INSIDE];
static int               s_count = 0;
static SemaphoreHandle_t s_lock;

void occupancy_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

/* Caller must already hold the lock. Returns -1 if not found. */
static int find_index(const char *uid)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_list[i].uid, uid) == 0) {
            return i;
        }
    }
    return -1;
}

bool occupancy_is_inside(const char *uid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool inside = (find_index(uid) >= 0);
    xSemaphoreGive(s_lock);
    return inside;
}

bool occupancy_add(const char *uid, const char *name)
{
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (find_index(uid) < 0 && s_count < MAX_INSIDE) {
        strlcpy(s_list[s_count].uid,  uid,  sizeof s_list[s_count].uid);
        strlcpy(s_list[s_count].name, name, sizeof s_list[s_count].name);
        s_list[s_count].since = time(NULL);
        s_count++;
        ok = true;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool occupancy_remove(const char *uid)
{
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int i = find_index(uid);
    if (i >= 0) {
        /* shift everyone after i one place to the left */
        memmove(&s_list[i], &s_list[i + 1],
                (size_t)(s_count - i - 1) * sizeof(occupant_t));
        s_count--;
        ok = true;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

int occupancy_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count;
    xSemaphoreGive(s_lock);
    return n;
}

int occupancy_list(occupant_t *out, int max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = (s_count < max) ? s_count : max;
    memcpy(out, s_list, (size_t)n * sizeof(occupant_t));
    xSemaphoreGive(s_lock);
    return n;
}