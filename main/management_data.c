#include "management_data.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

#include "occupancy.h"
#include "rooms.h"

#define ACCESS_LOG_PATH "/sdcard/access_log.csv"

// Room ids are hex topic ids such as "000101"; the room number is the value
// of the last two hex digits, so "000101" is room 1 and "000112" is room 18.
static int room_number_from_id(const char *id)
{
    size_t len = strlen(id);

    if (len < 2)
    {
        return -1;
    }

    return (int)strtol(id + len - 2, NULL, 16);
}

// Retrieve the current number of people inside the building
esp_err_t management_get_building_occupancy(int *count)
{
    if (count == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // The occupancy module already guards its own table
    *count = occupancy_count();

    return ESP_OK;
}

// Retrieve the occupancy status of a room
esp_err_t management_get_room_occupancy(int room_id, bool *occupied)
{
    if (room_id <= 0 || occupied == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    room_state_t rooms[MAX_ROOMS];
    int count = rooms_list(rooms, MAX_ROOMS);

    for (int i = 0; i < count; i++)
    {
        if (room_number_from_id(rooms[i].id) == room_id)
        {
            *occupied = rooms[i].occupied;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

// Retrieve the lighting status of a room
esp_err_t management_get_lighting_status(int room_id, bool *light_on)
{
    if (room_id <= 0 || light_on == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    room_state_t rooms[MAX_ROOMS];
    int count = rooms_list(rooms, MAX_ROOMS);

    for (int i = 0; i < count; i++)
    {
        if (room_number_from_id(rooms[i].id) == room_id)
        {
            *light_on = rooms[i].light;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

// Read the tail of access_log.csv, newest lines last, stopping when the
// caller's buffer is full. If uid is non-NULL, only lines containing that
// student id are kept.
static int access_records_copy(char *buffer, size_t buffer_size, const char *uid)
{
    FILE *f = fopen(ACCESS_LOG_PATH, "r");

    if (!f)
    {
        return 0;
    }

    // Read only the tail: the file only ever grows
    fseek(f, 0, SEEK_END);
    long size = ftell(f);

    long start = size > 1536 ? size - 1536 : 0;
    size_t span = (size_t)(size - start);

    char scratch[1536 + 1];
    fseek(f, start, SEEK_SET);
    span = fread(scratch, 1, span, f);
    fclose(f);

    scratch[span] = '\0';

    // Drop the (probably partial) first line when we cut into the middle
    char *lines = scratch;
    if (start > 0)
    {
        char *first_newline = strchr(scratch, '\n');
        if (first_newline)
        {
            lines = first_newline + 1;
        }
        else
        {
            lines = scratch + span;
        }
    }

    size_t used = 0;
    char *line = lines;

    while (*line)
    {
        char *newline = strchr(line, '\n');
        size_t line_len = newline ? (size_t)(newline - line) : strlen(line);

        int match = (uid == NULL) || (strstr(line, uid) != NULL);

        if (match && used + line_len + 1 < buffer_size)
        {
            memcpy(buffer + used, line, line_len);
            used += line_len;
            buffer[used++] = '\n';
            buffer[used] = '\0';
        }

        if (!newline)
        {
            break;
        }

        line = newline + 1;
    }

    return (int)used;
}

// Retrieve recent access records (newest last)
esp_err_t management_get_access_records(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';

    if (access_records_copy(buffer, buffer_size, NULL) == 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

// Retrieve access records for one student id (newest last)
esp_err_t management_get_access_records_for_uid(const char *uid,
                                                char *buffer,
                                                size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0 || uid == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0';

    if (access_records_copy(buffer, buffer_size, uid) == 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}
