#pragma once
#include <stdbool.h>
#include "hub_types.h"

/* Member B: look a card up in the user table. Returns true if found. */
bool user_lookup(const char *uid, user_t *out);

/* Member E: true while the building is inside its allowed period. */
bool schedule_is_open(void);

/* Member E: true while the authenticated emergency override is on. */
bool override_active(void);

/* Member B: record one access event (must return quickly). */
void log_access(door_side_t side, const char *uid, const char *name,
                access_result_t result, int inside_count);

/* Member B: record one room event (status/alert/online as received on MQTT). */
void log_room_event(const char *room_id, const char *what, const char *data);

/* Member B: record something the system itself did (boot, schedule change...). */
void log_system_event(const char *msg);