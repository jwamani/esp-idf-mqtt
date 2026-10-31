#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub_config.h"
#include "lcd_i2c.h"

static const char *TAG = "lcd";

/* Bits of the byte we send to the PCF8574 chip */
#define LCD_RS 0x01   /* 0 = command, 1 = character */
#define LCD_EN 0x04   /* pulse high then low to latch data */
#define LCD_BL 0x08   /* backlight on */

static i2c_master_dev_handle_t s_lcd;

static void expander_write(uint8_t value)
{
    i2c_master_transmit(s_lcd, &value, 1, 100);
}

static void write_nibble(uint8_t nibble, bool rs)
{
    uint8_t d = (uint8_t)((nibble << 4) | LCD_BL | (rs ? LCD_RS : 0));
    expander_write(d | LCD_EN);
    esp_rom_delay_us(2);
    expander_write(d & ~LCD_EN);
    esp_rom_delay_us(50);
}

static void send_byte(uint8_t value, bool rs)
{
    write_nibble(value >> 4, rs);
    write_nibble(value & 0x0F, rs);
}

void lcd_clear(void)
{
    send_byte(0x01, false);
    vTaskDelay(pdMS_TO_TICKS(5));
}

void lcd_print(int row, const char *text)
{
    send_byte((uint8_t)(0x80 | (row == 0 ? 0x00 : 0x40)), false);
    bool ended = false;
    for (int i = 0; i < 16; i++) {
        char c = ' ';
        if (!ended && text[i] != '\0') {
            c = text[i];
        } else {
            ended = true;
        }
        send_byte((uint8_t)c, true);
    }
}

void lcd_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    if (i2c_master_probe(bus, LCD_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "No LCD answered at address 0x%02X. Check SDA/SCL wiring.",
                 LCD_I2C_ADDR);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = LCD_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_lcd));

    /* Standard start-up sequence for a 16x2 screen in 4-bit mode */
    vTaskDelay(pdMS_TO_TICKS(50));
    write_nibble(0x03, false); vTaskDelay(pdMS_TO_TICKS(5));
    write_nibble(0x03, false); vTaskDelay(pdMS_TO_TICKS(5));
    write_nibble(0x03, false); vTaskDelay(pdMS_TO_TICKS(5));
    write_nibble(0x02, false);          /* switch to 4-bit mode      */
    send_byte(0x28, false);             /* 2 lines, 5x8 characters   */
    send_byte(0x0C, false);             /* display on, cursor off    */
    send_byte(0x06, false);             /* cursor moves to the right */
    lcd_clear();
    ESP_LOGI(TAG, "LCD ready");
}