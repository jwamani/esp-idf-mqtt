#include <stdio.h>
#include "app_config.h"
#include "net.h"
#include "hub_interfaces.h"
#include "occupancy.h"
#include "building.h"

/* The text the rooms expect in on_message(): "open", "closed" or "emergency". */
void building_announce(void)
{
    const char *state;
    if (override_active()) {
        state = "emergency";
    } else if (schedule_is_open()) {
        state = "open";
    } else {
        state = "closed";
    }

    char msg[40];
    snprintf(msg, sizeof msg, "%s inside=%d", state, occupancy_count());
    net_publish_retained(TOPIC_HUB, msg);
}
