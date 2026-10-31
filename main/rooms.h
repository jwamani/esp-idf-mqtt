#pragma once
#include <stdbool.h>

#define MAX_ROOMS 2

/* One room terminal, from the hub's point of view.
   Online/offline comes from the room's online message and the broker's
   last will, so nothing needs to be timed out on the hub side. */
typedef struct {
    char  id[8];      /* the hex part of the topic, e.g. "000116" */
    bool  occupied;
    bool  light;
    bool  online;
    int64_t last_seen;  /* ms; used only by rooms_tick if you enable it */

    /* the values already written to the log, so unchanged statuses
       do not fill the SD card (private to rooms.c) */
    bool lo_logged;
    bool occ_logged;
    bool light_logged;
    bool online_logged;
} room_state_t;

/* Create the (empty) room table. */
void rooms_init(void);

/* Feed one incoming message in: topic cocis-g1/rooms/<id>/status|alert|online.
   This only updates the table, it never waits on anything slow. */
void rooms_handle_message(const char *topic, const char *data);

/* Optional: flag rooms that stopped reporting as offline. Call it
   periodically if you want a safety net on top of the last will. */
void rooms_tick(void);

/* Copy the current table into out. Returns how many entries were filled. */
int  rooms_list(room_state_t *out, int max);
