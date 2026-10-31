#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hub_config.h"
#include "hub_types.h"
#include "rfid_reader.h"

static const char *TAG = "rfid";

/* MFRC522 register numbers */
#define REG_COMMAND      0x01
#define REG_COM_IRQ      0x04
#define REG_ERROR        0x06
#define REG_FIFO_DATA    0x09
#define REG_FIFO_LEVEL   0x0A
#define REG_CONTROL      0x0C
#define REG_BIT_FRAMING  0x0D
#define REG_COLL         0x0E
#define REG_MODE         0x11
#define REG_TX_CONTROL   0x14
#define REG_TX_ASK       0x15
#define REG_T_MODE       0x2A
#define REG_T_PRESCALER  0x2B
#define REG_T_RELOAD_H   0x2C
#define REG_T_RELOAD_L   0x2D
#define REG_VERSION      0x37

/* MFRC522 commands */
#define CMD_IDLE         0x00
#define CMD_TRANSCEIVE   0x0C
#define CMD_SOFT_RESET   0x0F

static QueueHandle_t      s_scan_queue;
static spi_device_handle_t s_dev[2];     /* 0 = ENTRY reader, 1 = EXIT reader */
static bool               s_ready[2];

/* ---------- Talking to the chip over SPI ---------- */

static void reg_write(int r, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), value };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    spi_device_polling_transmit(s_dev[r], &t);
}

static uint8_t reg_read(int r, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(0x80 | ((reg << 1) & 0x7E)), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(s_dev[r], &t);
    return rx[1];
}

static void fifo_write(int r, const uint8_t *data, int n)
{
    uint8_t tx[16];
    tx[0] = (uint8_t)((REG_FIFO_DATA << 1) & 0x7E);
    memcpy(&tx[1], data, (size_t)n);
    spi_transaction_t t = { .length = (size_t)(n + 1) * 8, .tx_buffer = tx };
    spi_device_polling_transmit(s_dev[r], &t);
}

static void fifo_read(int r, uint8_t *out, int n)
{
    uint8_t tx[17], rx[17];
    memset(tx, 0x80 | ((REG_FIFO_DATA << 1) & 0x7E), (size_t)n);
    tx[n] = 0x00;
    spi_transaction_t t = { .length = (size_t)(n + 1) * 8, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(s_dev[r], &t);
    memcpy(out, &rx[1], (size_t)n);
}

/* ---------- Talking to the card ---------- */

/* Send bytes to the card and collect its answer.
   Returns the number of answer bytes, or -1 if the card did not answer. */
static int transceive(int r, const uint8_t *send, int send_len,
                      uint8_t tx_last_bits, uint8_t *back, int back_cap)
{
    reg_write(r, REG_COMMAND, CMD_IDLE);
    reg_write(r, REG_COM_IRQ, 0x7F);          /* clear all interrupt flags */
    reg_write(r, REG_FIFO_LEVEL, 0x80);       /* empty the FIFO            */
    fifo_write(r, send, send_len);
    reg_write(r, REG_BIT_FRAMING, tx_last_bits);
    reg_write(r, REG_COMMAND, CMD_TRANSCEIVE);
    reg_write(r, REG_BIT_FRAMING, (uint8_t)(0x80 | tx_last_bits));   /* start */

    bool done = false;
    for (int i = 0; i < 40; i++) {
        uint8_t irq = reg_read(r, REG_COM_IRQ);
        if (irq & 0x30) {                     /* received something */
            done = true;
            break;
        }
        if (irq & 0x01) {                     /* chip timer ran out */
            return -1;
        }
        vTaskDelay(1);
    }
    if (!done) {
        return -1;
    }
    if (reg_read(r, REG_ERROR) & 0x1B) {      /* buffer, parity, protocol, collision */
        return -1;
    }
    int n = reg_read(r, REG_FIFO_LEVEL);
    if (n > back_cap) {
        return -1;
    }
    if (n > 0) {
        fifo_read(r, back, n);
    }
    return n;
}

/* Standard card checksum (CRC_A) */
static uint16_t crc_a(const uint8_t *data, int len)
{
    uint16_t crc = 0x6363;
    for (int i = 0; i < len; i++) {
        uint8_t b = (uint8_t)(data[i] ^ (crc & 0xFF));
        b = (uint8_t)(b ^ (uint8_t)(b << 4));
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ ((uint16_t)b >> 4));
    }
    return crc;
}

/* Is a card waiting in front of the reader? */
static bool card_request(int r)
{
    uint8_t cmd = 0x26;                       /* REQA: "is anybody there?" */
    uint8_t atqa[2];
    reg_write(r, REG_COLL, (uint8_t)(reg_read(r, REG_COLL) & ~0x80));
    return transceive(r, &cmd, 1, 7, atqa, sizeof atqa) == 2;
}

/* Read the card number. Handles 4-byte and 7-byte numbers.
   Returns how many bytes were stored in uid, or 0 on failure. */
static int card_read_uid(int r, uint8_t *uid)
{
    static const uint8_t sel[2] = { 0x93, 0x95 };
    int uid_len = 0;

    for (int level = 0; level < 2; level++) {
        uint8_t buf[9], ac[8], sak[8];

        /* 1. Ask the card to send its number */
        buf[0] = sel[level];
        buf[1] = 0x20;
        reg_write(r, REG_COLL, (uint8_t)(reg_read(r, REG_COLL) & ~0x80));
        int n = transceive(r, buf, 2, 0, ac, sizeof ac);
        if (n != 5 || (uint8_t)(ac[0] ^ ac[1] ^ ac[2] ^ ac[3]) != ac[4]) {
            return 0;
        }

        /* 2. Select that card, so only it keeps talking */
        buf[0] = sel[level];
        buf[1] = 0x70;
        memcpy(&buf[2], ac, 5);
        uint16_t crc = crc_a(buf, 7);
        buf[7] = (uint8_t)(crc & 0xFF);
        buf[8] = (uint8_t)(crc >> 8);
        n = transceive(r, buf, 9, 0, sak, sizeof sak);
        if (n != 3) {
            return 0;
        }

        if (ac[0] == 0x88) {                  /* number continues in level 2 */
            memcpy(&uid[uid_len], &ac[1], 3);
            uid_len += 3;
        } else {
            memcpy(&uid[uid_len], ac, 4);
            uid_len += 4;
            return uid_len;
        }
    }
    return 0;
}

/* Tell the card to stay quiet until it is taken away and brought back */
static void card_halt(int r)
{
    uint8_t buf[4] = { 0x50, 0x00, 0, 0 };
    uint8_t dummy[4];
    uint16_t crc = crc_a(buf, 2);
    buf[2] = (uint8_t)(crc & 0xFF);
    buf[3] = (uint8_t)(crc >> 8);
    transceive(r, buf, 4, 0, dummy, sizeof dummy);   /* a halted card does not answer */
}

/* ---------- Reader start-up ---------- */

static bool reader_init(int r)
{
    reg_write(r, REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i < 5 && (reg_read(r, REG_COMMAND) & 0x10); i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint8_t version = reg_read(r, REG_VERSION);
    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "%s reader does not answer (version 0x%02X). Check its wiring.",
                 r == SIDE_ENTRY ? "ENTRY" : "EXIT", version);
        return false;
    }

    reg_write(r, REG_T_MODE, 0x80);           /* timer starts by itself   */
    reg_write(r, REG_T_PRESCALER, 0xA9);
    reg_write(r, REG_T_RELOAD_H, 0x03);       /* about 25 ms timeout      */
    reg_write(r, REG_T_RELOAD_L, 0xE8);
    reg_write(r, REG_TX_ASK, 0x40);
    reg_write(r, REG_MODE, 0x3D);

    uint8_t tx = reg_read(r, REG_TX_CONTROL); /* switch the antenna on    */
    if ((tx & 0x03) != 0x03) {
        reg_write(r, REG_TX_CONTROL, (uint8_t)(tx | 0x03));
    }
    ESP_LOGI(TAG, "%s reader started (chip version 0x%02X)",
             r == SIDE_ENTRY ? "ENTRY" : "EXIT", version);
    return true;
}

/* ---------- From card number to a message for the decision task ---------- */

static void report(int side, const uint8_t *uid, int len)
{
    scan_event_t scan;
    scan.side = (door_side_t)side;
    char *p = scan.uid;
    for (int i = 0; i < len && i < 10; i++) {
        snprintf(p, 3, "%02X", uid[i]);
        p += 2;
    }
    *p = '\0';

    /* Debounce: ignore the same card on the same reader if it is seen
       again within SCAN_DEBOUNCE_MS. */
    static int64_t last_ms[2];
    static char    last_uid[2][UID_STR_LEN];
    int64_t now = esp_timer_get_time() / 1000;
    bool repeat = (strcmp(last_uid[side], scan.uid) == 0) &&
                  (now - last_ms[side] < SCAN_DEBOUNCE_MS);
    last_ms[side] = now;
    strcpy(last_uid[side], scan.uid);
    if (repeat) {
        return;
    }

    ESP_LOGI(TAG, "card %s on %s reader", scan.uid,
             side == SIDE_ENTRY ? "ENTRY" : "EXIT");

    if (xQueueSend(s_scan_queue, &scan, 0) != pdTRUE) {
        ESP_LOGW(TAG, "scan queue full, tap dropped");
    }
}

/* One task checks both readers, one after the other */
static void rfid_task(void *arg)
{
    while (1) {
        for (int r = 0; r < 2; r++) {
            if (!s_ready[r]) {
                continue;
            }
            if (card_request(r)) {
                uint8_t uid[10];
                int len = card_read_uid(r, uid);
                if (len > 0) {
                    card_halt(r);
                    report(r, uid, len);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void rfid_init(QueueHandle_t scan_queue)
{
    s_scan_queue = scan_queue;

    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_RFID_MOSI,
        .miso_io_num     = PIN_RFID_MISO,
        .sclk_io_num     = PIN_RFID_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    const int cs_pin[2] = { PIN_RFID_CS_ENTRY, PIN_RFID_CS_EXIT };
    for (int r = 0; r < 2; r++) {
        spi_device_interface_config_t dev = {
            .clock_speed_hz = 1000000,
            .mode           = 0,
            .spics_io_num   = cs_pin[r],
            .queue_size     = 1,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_dev[r]));
        s_ready[r] = reader_init(r);
    }

    xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 5, NULL);
}