#pragma once
#include <stdbool.h>
#include "hub_types.h"

/* Set up LCD, LEDs, servos and start the door task. */
void door_init(void);

/* Show a result at one door. granted = open the door + green LED,
   otherwise keep it closed + red LED. line1/line2 go on the LCD. */
void door_show_result(door_side_t side, bool granted,
                      const char *line1, const char *line2);

/* Member E calls this when the emergency override changes.
   on = true holds the EXIT door open until called again with false. */
void door_emergency_unlock(bool on);