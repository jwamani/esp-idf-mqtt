#pragma once
#include "hub_types.h"

/* Start everything: occupancy list, doors, decision task, RFID readers. */
void access_init(void);

/* Feed a tap in by hand (same path as a real card). Useful for tests
   and for a typed "tap" command if the RFID part ever fails. */
void access_submit_scan(door_side_t side, const char *uid);

/* Short text for a result, e.g. "Outside hours". At most 16 characters. */
const char *access_result_text(access_result_t r);