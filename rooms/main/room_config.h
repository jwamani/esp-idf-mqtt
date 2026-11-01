// room_config.h - pins and timings for the room terminal (owner: rooms)
#pragma once

// ---- pins ----
#define PIN_PIR              4
#define PIN_LIGHT            5
#define PIN_LDR              6
#define PIN_TRIG             7
#define PIN_ECHO             15

// ---- presence detection ----
#define EMPTY_DIST_CM        300     // distance with nobody in the beam
#define OBJECT_MARGIN_CM     50      // closer than (300-50)=250 cm means someone is there

// ---- light logic ----
#define VACANCY_TIMEOUT_MS   15000   // demo value; 600000 (10 min) in real life
#define WARN_MS              5000    // blink this long before switching off
#define LDR_DARK_LEVEL       1     // Wokwi LDR: DO goes HIGH when dark, LOW when bright

// ---- reporting to the hub ----
#define STATUS_PERIOD_MS     5000    // send status at least this often
#define INTRUSION_COOLDOWN_MS 10000  // at most one alert per 10 s