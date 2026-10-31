#pragma once

/* Publish the building state (retained) on cocis-g1/building:
       open inside=N        normal operation, access allowed
       closed inside=N      outside the allowed period
       emergency inside=N   the emergency override is on
   Call this periodically and whenever the schedule or override changes. */
void building_announce(void);
