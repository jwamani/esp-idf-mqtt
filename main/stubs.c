#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "hub_config.h"
#include "hub_interfaces.h"

#if USE_STUBS

static const char *TAG = "stub";

/* The user table and log_access() now live in storage.c (member B).
   The stubs below are the fakes members E replaces: */

bool schedule_is_open(void)
{
    static int last = -1;
    int64_t sec = esp_timer_get_time() / 1000000;
    bool open = (sec % (STUB_OPEN_SECONDS + STUB_CLOSED_SECONDS)) < STUB_OPEN_SECONDS;
    if (last != (int)open) {
        last = (int)open;
        ESP_LOGW(TAG, "Building is now %s", open ? "OPEN" : "CLOSED");
    }
    return open;
}

bool override_active(void)
{
    return false;
}

#endif /* USE_STUBS */