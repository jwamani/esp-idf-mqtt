#include <stdio.h>
#include <string.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hub_config.h"
#include "hub_interfaces.h"
#include "occupancy.h"
#include "lcd_i2c.h"
#include "door_ctrl.h"

static const char *TAG = "door";

typedef enum { CMD_RESULT, CMD_EMERGENCY_ON, CMD_EMERGENCY_OFF } cmd_type_t;

typedef struct {
    cmd_type_t  type;
    door_side_t side;
    bool        granted;
    char        line1[17];
    char        line2[17];
} door_cmd_t;

static QueueHandle_t s_cmd_queue;

/* Index 0 = ENTRY door, index 1 = EXIT door */
static const int s_green_pin[2] = { PIN_LED_ENTRY_GREEN, PIN_LED_EXIT_GREEN };
static const int s_red_pin[2]   = { PIN_LED_ENTRY_RED,   PIN_LED_EXIT_RED };
static const int s_servo_pin[2] = { PIN_SERVO_ENTRY,     PIN_SERVO_EXIT };
static const ledc_channel_t s_servo_ch[2] = { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void servo_set_angle(int door, int degrees)
{
    uint32_t pulse_us = 500 + (uint32_t)degrees * 2000 / 180;  /* 500 to 2500 us */
    uint32_t duty     = pulse_us * 16384 / 20000;              /* 14-bit, 20 ms  */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, s_servo_ch[door], duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, s_servo_ch[door]);
}

static void leds_set(int door, bool green, bool red)
{
    gpio_set_level(s_green_pin[door], green);
    gpio_set_level(s_red_pin[door], red);
}

static void hardware_init(void)
{
    for (int i = 0; i < 2; i++) {
        gpio_reset_pin(s_green_pin[i]);
        gpio_set_direction(s_green_pin[i], GPIO_MODE_OUTPUT);
        gpio_reset_pin(s_red_pin[i]);
        gpio_set_direction(s_red_pin[i], GPIO_MODE_OUTPUT);
        leds_set(i, false, false);
    }

    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 50,              /* one pulse every 20 ms */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch = {
            .gpio_num   = s_servo_pin[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = s_servo_ch[i],
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = LEDC_TIMER_0,
            .duty       = 0,
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
        servo_set_angle(i, SERVO_CLOSED_DEG);
    }
}

static void door_task(void *arg)
{
    int64_t close_at[2]   = { 0, 0 };   /* when to close each door (0 = closed) */
    int64_t led_off_at[2] = { 0, 0 };   /* when to switch each LED pair off     */
    int64_t message_until = 0;          /* LCD shows a result until this time   */
    int64_t last_refresh  = 0;
    bool    emergency     = false;
    door_cmd_t cmd;

    while (1) {
        /* Wait up to 100 ms for a command, then do the timer checks below */
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            int64_t t0 = now_ms();
            int d = (int)cmd.side;

            switch (cmd.type) {
            case CMD_RESULT:
                lcd_print(0, cmd.line1);
                lcd_print(1, cmd.line2);
                message_until = t0 + MESSAGE_SHOW_MS;
                if (cmd.granted) {
                    leds_set(d, true, false);
                    servo_set_angle(d, SERVO_OPEN_DEG);
                    close_at[d]   = t0 + DOOR_OPEN_MS;
                    led_off_at[d] = t0 + DOOR_OPEN_MS;
                } else {
                    leds_set(d, false, true);
                    led_off_at[d] = t0 + DENY_LED_MS;
                }
                break;

            case CMD_EMERGENCY_ON:
                emergency = true;
                servo_set_angle(SIDE_EXIT, SERVO_OPEN_DEG);
                leds_set(SIDE_EXIT, true, false);
                break;

            case CMD_EMERGENCY_OFF:
                emergency = false;
                servo_set_angle(SIDE_EXIT, SERVO_CLOSED_DEG);
                leds_set(SIDE_EXIT, false, false);
                close_at[SIDE_EXIT]   = 0;
                led_off_at[SIDE_EXIT] = 0;
                message_until = 0;
                break;
            }
        }

        int64_t t = now_ms();

        /* Close doors and switch LEDs off when their time is up.
           The EXIT door stays open while the emergency override is on. */
        for (int d = 0; d < 2; d++) {
            bool held_open = (emergency && d == SIDE_EXIT);
            if (close_at[d] != 0 && t >= close_at[d] && !held_open) {
                servo_set_angle(d, SERVO_CLOSED_DEG);
                close_at[d] = 0;
            }
            if (led_off_at[d] != 0 && t >= led_off_at[d] && !held_open) {
                leds_set(d, false, false);
                led_off_at[d] = 0;
            }
        }

        /* Idle screen, refreshed once a second when no result is showing */
        if (t >= message_until && t - last_refresh >= 1000) {
            last_refresh = t;
            if (emergency) {
                lcd_print(0, "EMERGENCY MODE");
                lcd_print(1, "Exit unlocked");
            } else {
                char line1[40];
                time_t now = time(NULL);
                struct tm tm_now;
                localtime_r(&now, &tm_now);
                if (tm_now.tm_year >= 120) {      /* clock has been set */
                    snprintf(line1, sizeof line1, "Inside:%-3d %02d:%02d",
                             occupancy_count(), tm_now.tm_hour, tm_now.tm_min);
                } else {
                    snprintf(line1, sizeof line1, "Inside: %d", occupancy_count());
                }
                lcd_print(0, line1);
                lcd_print(1, schedule_is_open() ? "Scan your card" : "Building CLOSED");
            }
        }
    }
}

void door_init(void)
{
    lcd_init();
    hardware_init();
    s_cmd_queue = xQueueCreate(8, sizeof(door_cmd_t));
    xTaskCreate(door_task, "door_task", 5120, NULL, 5, NULL);
    ESP_LOGI(TAG, "doors ready");
}

void door_show_result(door_side_t side, bool granted,
                      const char *line1, const char *line2)
{
    door_cmd_t cmd = { .type = CMD_RESULT, .side = side, .granted = granted };
    strlcpy(cmd.line1, line1, sizeof cmd.line1);
    strlcpy(cmd.line2, line2, sizeof cmd.line2);
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100));
}

void door_emergency_unlock(bool on)
{
    door_cmd_t cmd = { .type = on ? CMD_EMERGENCY_ON : CMD_EMERGENCY_OFF,
                       .side = SIDE_EXIT };
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100));
}