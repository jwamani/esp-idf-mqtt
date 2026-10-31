#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/spi_common.h"
#include "driver/sdspi_host.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "hub_config.h"
#include "hub_types.h"
#include "access_ctrl.h"
#include "storage.h"

static const char *TAG = "storage";

#define MOUNT_POINT "/sdcard"

/* The user table lives in RAM: lookups are fast and the SD card is only
   touched at boot (to load) and when a user is added/deleted (to rewrite). */
#define MAX_USERS 50

static user_t s_users[MAX_USERS];
static int    s_user_count = 0;

/* guards the RAM user table: the decision task looks users up while the
   console (via users_add/users_delete) can change the table at any moment */
static SemaphoreHandle_t s_users_lock;

/* false once we know the card is not usable; every log call checks it */
static bool s_card_ok = false;

static sdmmc_card_t *s_card;

/* ---------- The card and its files ---------- */

static bool append_line(const char *path, const char *line)
{
    if (!s_card_ok) {
        return false;
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", path);
        s_card_ok = false;          // card disappeared? stop trying until reboot
        return false;
    }
    bool ok = (fprintf(f, "%s\n", line) >= 0);
    if (fclose(f) != 0) {
        ok = false;
    }
    if (!ok) {
        ESP_LOGE(TAG, "write failed on %s", path);
        s_card_ok = false;
    }
    return ok;
}

static void timestamp(char *out, int cap)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 120) {                 // SNTP has not set the clock yet
        snprintf(out, cap, "no-time");
        return;
    }
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &t);
}

/* users.csv rows: uid,name,role,enabled   e.g. 01020304,Alice,admin,1 */
static void load_users(void)
{
    FILE *f = fopen(MOUNT_POINT "/users.csv", "r");
    if (!f) {
        ESP_LOGW(TAG, "users.csv missing, creating a demo one");
        f = fopen(MOUNT_POINT "/users.csv", "w");
        if (!f) {
            ESP_LOGE(TAG, "cannot create users.csv");
            s_card_ok = false;
            return;
        }
        fprintf(f,
                "uid,name,role,enabled\n"          // header, skipped below
                "01020304,Alice,admin,1\n"         // Wokwi Blue card
                "11223344,Bob,staff,1\n"           // Wokwi Green card
                "55667788,Carol,student,1\n"       // Wokwi Yellow card
                "C0FFEE99,Dave,student,0\n");      // disabled card for tests
        fclose(f);

        f = fopen(MOUNT_POINT "/users.csv", "r");
        if (!f) {
            return;
        }
    }

    s_user_count = 0;
    char line[96];
    while (s_user_count < MAX_USERS && fgets(line, sizeof line, f)) {
        char uid[UID_STR_LEN], name[16], role[8];
        int enabled = 0;
        if (line[0] == 'u' && line[1] == 'i' && line[2] == 'd') {
            continue;                              // skip the header line
        }
        if (sscanf(line, "%20[^,],%15[^,],%7[^,],%d", uid, name, role, &enabled) == 4) {
            user_t *u = &s_users[s_user_count++];
            memset(u, 0, sizeof *u);
            strlcpy(u->uid, uid, sizeof u->uid);
            strlcpy(u->name, name, sizeof u->name);
            strlcpy(u->role, role, sizeof u->role);
            u->enabled = (enabled != 0);
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "loaded %d users from users.csv", s_user_count);
}

/* ---------- What other files call ---------- */

void storage_init(void)
{
    s_users_lock = xSemaphoreCreateMutex();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;                         // SD has its own bus

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD SPI bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = host.slot;

    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s (running without storage)", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return;
    }

    s_card_ok = true;
    sdmmc_card_print_info(stdout, s_card);

    load_users();
    log_system_event("hub boot: storage ready");
}

bool user_lookup(const char *uid, user_t *out)
{
    bool found = false;

    if (!s_users_lock) {                 // storage_init was never reached
        return false;
    }
    xSemaphoreTake(s_users_lock, portMAX_DELAY);
    for (int i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].uid, uid) == 0) {
            *out = s_users[i];
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_users_lock);
    return found;
}

/* rewrite users.csv from the RAM table (the file is the mirror of the table) */
static void users_save(void)
{
    if (!s_card_ok) {
        ESP_LOGW(TAG, "no card, user change kept in RAM only");
        return;
    }
    FILE *f = fopen(MOUNT_POINT "/users.csv", "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot rewrite users.csv");
        return;
    }
    fprintf(f, "uid,name,role,enabled\n");
    for (int i = 0; i < s_user_count; i++) {
        fprintf(f, "%s,%s,%s,%d\n", s_users[i].uid, s_users[i].name,
                s_users[i].role, s_users[i].enabled ? 1 : 0);
    }
    fclose(f);
    ESP_LOGI(TAG, "users.csv rewritten (%d users)", s_user_count);
}

bool users_add(const char *uid, const char *name, const char *role, bool enabled)
{
    if (!s_users_lock) {
        return false;
    }

    xSemaphoreTake(s_users_lock, portMAX_DELAY);
    /* full table or uid already present? (check inline: user_lookup would try
       to take the same non-recursive mutex) */
    if (s_user_count >= MAX_USERS) {
        xSemaphoreGive(s_users_lock);
        ESP_LOGW(TAG, "user table full");
        return false;
    }
    for (int i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].uid, uid) == 0) {
            xSemaphoreGive(s_users_lock);
            ESP_LOGW(TAG, "uid %s already exists", uid);
            return false;
        }
    }
    user_t *row = &s_users[s_user_count++];
    memset(row, 0, sizeof *row);
    strlcpy(row->uid, uid, sizeof row->uid);
    strlcpy(row->name, name, sizeof row->name);
    strlcpy(row->role, role, sizeof row->role);
    row->enabled = enabled;
    xSemaphoreGive(s_users_lock);

    users_save();
    return true;
}

bool users_delete(const char *uid)
{
    if (!s_users_lock) {
        return false;
    }

    bool found = false;
    xSemaphoreTake(s_users_lock, portMAX_DELAY);
    for (int i = 0; i < s_user_count; i++) {
        if (strcmp(s_users[i].uid, uid) == 0) {
            memmove(&s_users[i], &s_users[i + 1],
                    (size_t)(s_user_count - i - 1) * sizeof(user_t));
            s_user_count--;
            found = true;
            break;
        }
    }
    xSemaphoreGive(s_users_lock);

    if (found) {
        users_save();
        return true;
    }
    ESP_LOGW(TAG, "uid %s not found", uid);
    return false;
}

void log_access(door_side_t side, const char *uid, const char *name,
                access_result_t result, int inside_count)
{
    char ts[24];
    timestamp(ts, sizeof ts);

    ESP_LOGI("log", "%s uid=%s name=%s -> %s (inside=%d)", side == SIDE_ENTRY ? "ENTRY" : "EXIT", uid, name, access_result_text(result), inside_count);

    char line[128];
    snprintf(line, sizeof line, "%s,%s,%s,%s,%s,%d", ts, side == SIDE_ENTRY ? "ENTRY" : "EXIT", uid, name, access_result_text(result), inside_count);
    if (!append_line(MOUNT_POINT "/access_log.csv", line)) {
        ESP_LOGW(TAG, "access event not saved: %s", line);
    }
}

void log_room_event(const char *room_id, const char *what, const char *data)
{
    char ts[24];
    timestamp(ts, sizeof ts);

    char line[160];
    snprintf(line, sizeof line, "%s,%s,%s,%s", ts, room_id, what, data);
    if (!append_line(MOUNT_POINT "/room_log.csv", line)) {
        ESP_LOGW(TAG, "room event not saved: %s", line);
    }
}

void log_system_event(const char *msg)
{
    char ts[24];
    timestamp(ts, sizeof ts);

    char line[192];
    snprintf(line, sizeof line, "%s,%s", ts, msg);

    ESP_LOGI("system", "%s", msg);
    if (!append_line(MOUNT_POINT "/system.log", line)) {
        ESP_LOGW(TAG, "system event not saved: %s", msg);
    }
}
