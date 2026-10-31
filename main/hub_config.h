#pragma once

/* ---------- Pin map (the team's agreed map) ---------- */
#define PIN_RFID_SCK         12
#define PIN_RFID_MOSI        11
#define PIN_RFID_MISO        13
#define PIN_RFID_CS_ENTRY    10
#define PIN_RFID_CS_EXIT      9

#define PIN_SERVO_ENTRY      15
#define PIN_SERVO_EXIT       16

#define PIN_I2C_SDA          17
#define PIN_I2C_SCL          18
#define LCD_I2C_ADDR       0x27

/* ---------- SD card (own SPI bus, separate from the RFID bus) ---------- */
#define PIN_SD_SCK            4
#define PIN_SD_MOSI           5
#define PIN_SD_MISO           6
#define PIN_SD_CS             7

#define PIN_LED_ENTRY_GREEN   1
#define PIN_LED_ENTRY_RED     2
#define PIN_LED_EXIT_GREEN   38
#define PIN_LED_EXIT_RED     39

/* ---------- Behaviour settings ---------- */
#define SERVO_CLOSED_DEG      0
#define SERVO_OPEN_DEG       90
#define DOOR_OPEN_MS       5000   /* how long a door stays open     */
#define DENY_LED_MS        2000   /* how long a red LED stays on    */
#define MESSAGE_SHOW_MS    3000   /* how long an LCD message stays  */
#define SCAN_DEBOUNCE_MS   2000   /* ignore the same card this soon */
#define MAX_INSIDE           50   /* size of the inside list        */

/* ---------- Stand-ins for teammates' modules ---------- */
#define USE_STUBS             1   /* 1 = use fakes, 0 = use real ones */
#define STUB_OPEN_SECONDS   150   /* fake schedule: open this long ... */
#define STUB_CLOSED_SECONDS  60   /* ... then closed this long, repeat */