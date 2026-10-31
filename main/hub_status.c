#include <stdio.h>
#include <time.h>

#include "hub_types.h"
#include "occupancy.h"
#include "rooms.h"

/* The application's extra status section, registered with the management
   component's 'status' command (management_register_status_ext). It shows
   what the coursework's serial-interface requirement asks for: who is
   inside the building right now, and each room's occupancy/light/online
   state. Access records live in access_log.csv on the SD card. */

#define MAX_INSIDE 30
#define MAX_ROOMS_PRINT 8

static void print_who_is_inside(void)
{
    occupant_t inside[MAX_INSIDE];
    int n = occupancy_list(inside, MAX_INSIDE);

    printf("Inside now: %d\n", n);
    for (int i = 0; i < n; i++)
    {
        time_t since = time(NULL) - inside[i].since;
        long minutes = (long)(since / 60);
        printf("  %s %s (%ld min)\n", inside[i].uid, inside[i].name, minutes);
    }
    printf("\n");
}

static void print_rooms(void)
{
    room_state_t rooms[MAX_ROOMS_PRINT];
    int n = rooms_list(rooms, MAX_ROOMS_PRINT);

    printf("Rooms:\n");
    for (int i = 0; i < n; i++)
    {
        printf("  %s: %s, light %s, %s\n", rooms[i].id, rooms[i].occupied ? "occupied" : "vacant", rooms[i].light ? "ON" : "off", rooms[i].online ? "online" : "OFFLINE");
    }
}

void print_hub_status_ext(void)
{
    printf("----------- occupancy -----------\n");
    print_who_is_inside();

    printf("----------- rooms ---------------\n");
    print_rooms();
}
