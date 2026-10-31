#ifndef MANAGEMENT_DATA_H
#define MANAGEMENT_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Retrieve recent access records (newest last).
esp_err_t management_get_access_records(char *buffer, size_t buffer_size);

// Retrieve access records for one student id (newest last).
esp_err_t management_get_access_records_for_uid(const char *uid,
                                                char *buffer,
                                                size_t buffer_size);

// Retrieve the current number of people inside the building
esp_err_t management_get_building_occupancy(int *count);

// Retrieve the occupancy status of a room
esp_err_t management_get_room_occupancy(int room_id, bool *occupied);

// Retrieve the lighting status of a room
esp_err_t management_get_lighting_status(int room_id, bool *light_on);

#endif
