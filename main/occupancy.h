#pragma once
#include <stdbool.h>
#include "hub_types.h"

void occupancy_init(void);
bool occupancy_is_inside(const char *uid);
bool occupancy_add(const char *uid, const char *name);    /* false if already in or full */
bool occupancy_remove(const char *uid);                   /* false if not inside */
int  occupancy_count(void);
/* Copy up to max people into out. Returns how many were copied. */
int  occupancy_list(occupant_t *out, int max);