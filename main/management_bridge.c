#include "management.h"
#include "hub_interfaces.h"

/* The schedule and emergency override come from the management component
   (the real console module). The access logic only knows the
   hub_interfaces.h signatures; this file is the bridge between the two. */

bool schedule_is_open(void)
{
    return management_is_open_now();
}

bool override_active(void)
{
    return management_emergency_exit_allowed();
}
