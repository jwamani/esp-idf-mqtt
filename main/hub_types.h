#pragma once
#include <stdbool.h>
#include <time.h>

#define UID_STR_LEN 21   /* up to 10 bytes = 20 hex characters + end marker */

typedef enum { SIDE_ENTRY = 0, SIDE_EXIT = 1 } door_side_t;

/* One row of the user table (Member B owns the real table) */
typedef struct {
    char uid[UID_STR_LEN];
    char name[16];
    char role[8];
    bool enabled;
} user_t;

/* A card tap: which reader, which card */
typedef struct {
    door_side_t side;
    char uid[UID_STR_LEN];
} scan_event_t;

/* One person currently inside the building */
typedef struct {
    char   uid[UID_STR_LEN];
    char   name[16];
    time_t since;
} occupant_t;

/* The outcome of an access decision */
typedef enum {
    ACCESS_GRANTED = 0,
    ACCESS_EMERGENCY_EXIT,
    DENIED_UNKNOWN,
    DENIED_DISABLED,
    DENIED_OUTSIDE_HOURS,
    DENIED_ALREADY_INSIDE,
    DENIED_NOT_INSIDE
} access_result_t;