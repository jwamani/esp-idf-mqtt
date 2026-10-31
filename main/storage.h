#pragma once
#include <stdbool.h>
#include "hub_types.h"

/* SD card storage (member B). Mounts the card, reads users.csv into RAM,
   and owns the append-only CSV files. Everything logs to the serial
   monitor too, so the system still works without a card. */

/* Mount the card and load the user table. Call once, before access_init(). */
void storage_init(void);

/* Member B: look a card up in the user table loaded from users.csv.
   Returns true if found. (hub_interfaces.h contract) */
bool user_lookup(const char *uid, user_t *out);

/* Append one access event to access_log.csv. (hub_interfaces.h contract) */
void log_access(door_side_t side, const char *uid, const char *name,
                access_result_t result, int inside_count);

/* Append one room event to room_log.csv. Rooms report every 5 s, so the
   hub only calls this when something actually changes. */
void log_room_event(const char *room_id, const char *what, const char *data);

/* Append one line to system.log (boots, and later schedule/auth events). */
void log_system_event(const char *msg);

/* Admin user management (the serial CLI calls these after a login).
   They only change the RAM table; users_save() rewrites users.csv. */
bool users_add(const char *uid, const char *name, const char *role, bool enabled);
bool users_delete(const char *uid);
