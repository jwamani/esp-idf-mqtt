// light.h - what light.c offers to the rest of the program (owner: rooms)
#pragma once
#include <stdbool.h>
#include <stdint.h>

void light_start(void);                    // call once, at the start of app_main
void light_set_building_open(bool open);   // hub says OPEN (true) or CLOSED (false)

bool light_is_on(void);
bool light_is_occupied(void);              // someone seen recently

uint32_t light_on_seconds(void);           // total time the lamp has been on
uint32_t light_open_seconds(void);         // total time the building has been open
uint32_t light_intrusion_count(void);      // goes up by 1 for each intrusion alert