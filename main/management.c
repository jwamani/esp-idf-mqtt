#include <stdio.h>
#include <stdlib.h>
#include "management.h"
#include "esp_log.h"
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_console.h"
#include "management_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Identifies messages from our management module
static const char *TAG = "MANAGEMENT";

// Default building opening and closing hours
static int opening_hour = 8;
static int opening_minute = 0;

static int closing_hour = 17;
static int closing_minute = 0;

// Guards the schedule and override state: the console task writes it while
// the decision task and the reporting loop read it at any moment
static SemaphoreHandle_t sched_lock = NULL;

// Emergency override starts disabled
static bool emergency_override = false;

// Tracks whether the module has been initialized
static bool initialized = false;

// Tracks whether an administrator is authenticated
static bool admin_logged_in = false;

// Temporary demonstration PIN
static const char *admin_pin = "2468";

// Maximum number of consecutive incorrect PIN attempts
#define MAX_LOGIN_ATTEMPTS 3

// Lockout duration in microseconds (60 seconds)
#define LOCKOUT_DURATION_US (60LL * 1000000LL)

// Number of consecutive failed login attempts
static int failed_login_attempts = 0;

// Time when the current lockout expires
static int64_t lockout_until = 0;

#define SESSION_TIMEOUT_US (5LL * 60LL * 1000000LL)

// Time of the administrator's last activity
static int64_t last_admin_activity = 0;

// extra status printer registered by the application (occupancy, rooms etc.)
static void (*status_ext)(void) = NULL;

// Register or clear the application's extra status printer
void management_register_status_ext(void (*fn)(void))
{
    status_ext = fn;
}

// Initialize the management system
esp_err_t management_init(void)
{
    if (initialized)
    {
        return ESP_OK;
    }

    if (sched_lock == NULL)
    {
        sched_lock = xSemaphoreCreateMutex();
    }

    emergency_override = false;
    admin_logged_in = false;

    failed_login_attempts = 0;
    lockout_until = 0;
    last_admin_activity = 0;
    // Administrator session timeout: 5 minutes

    initialized = true;

    ESP_LOGI(TAG, "Management system initialized");

    ESP_LOGI(TAG, "Opening time: %02d:%02d",
             opening_hour, opening_minute);

    ESP_LOGI(TAG, "Closing time: %02d:%02d",
             closing_hour, closing_minute);

    return ESP_OK;
}
// Check whether a given time falls within building opening hours
static bool is_within_schedule(int hour, int minute)
{
    int current_time = hour * 60 + minute;
    bool open_now;

    // The open and close times move together: read as one consistent snapshot
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    int opening_time = opening_hour * 60 + opening_minute;

    int closing_time = closing_hour * 60 + closing_minute;
    xSemaphoreGive(sched_lock);

    open_now = (current_time >= opening_time &&
                current_time < closing_time);
    return open_now;
}

// Check whether the building is currently open
bool management_is_open_now(void)
{
    time_t now;
    struct tm time_info;

    time(&now);

    localtime_r(&now, &time_info);

    return is_within_schedule(
        time_info.tm_hour,
        time_info.tm_min);
}

// Determine whether normal building access is allowed
bool management_access_allowed(void)
{
    if (!initialized)
    {
        return false;
    }

    return management_is_open_now();
}

// Check whether emergency exit override is enabled
bool management_emergency_exit_allowed(void)
{
    if (!initialized)
    {
        return false;
    }

    bool enabled;
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    enabled = emergency_override;
    xSemaphoreGive(sched_lock);
    return enabled;
}

// Update the building's operating hours
esp_err_t management_set_schedule(
    int open_hour,
    int open_minute,
    int close_hour,
    int close_minute)
{
    // Only an authenticated administrator can update the schedule
    if (!management_admin_is_logged_in())
    {
        ESP_LOGW(TAG, "Access denied. Administrator login required.");
        return ESP_ERR_INVALID_STATE;
    }

    // Check that hours and minutes are valid
    if (open_hour < 0 || open_hour > 23 ||
        close_hour < 0 || close_hour > 23 ||
        open_minute < 0 || open_minute > 59 ||
        close_minute < 0 || close_minute > 59)
    {

        ESP_LOGE(TAG, "Invalid time entered");
        management_admin_update_activity();
        return ESP_ERR_INVALID_ARG;
    }

    // Convert both times into minutes
    int opening = open_hour * 60 + open_minute;
    int closing = close_hour * 60 + close_minute;

    // Closing time must be later than opening time
    if (opening >= closing)
    {
        ESP_LOGE(TAG, "Closing time must be after opening time");
        return ESP_ERR_INVALID_ARG;
    }

    // Update the schedule atomically: the whole new schedule lands under one lock
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    opening_hour = open_hour;
    opening_minute = open_minute;

    closing_hour = close_hour;
    closing_minute = close_minute;
    xSemaphoreGive(sched_lock);

    ESP_LOGI(TAG, "Building schedule updated");

    ESP_LOGI(TAG, "New opening time: %02d:%02d", opening_hour, opening_minute);

    ESP_LOGI(TAG, "New closing time: %02d:%02d", closing_hour, closing_minute);

    return ESP_OK;
}

// Authenticate an administrator
// Authenticate an administrator with login attempt protection
bool management_admin_login(const char *pin)
{
    if (!initialized || pin == NULL)
    {
        return false;
    }

    // Get the current time in microseconds
    int64_t current_time = esp_timer_get_time();

    // Check whether the system is currently locked
    if (current_time < lockout_until)
    {
        ESP_LOGW(TAG, "Login temporarily locked. Try again later.");
        return false;
    }

    // Check whether the PIN is correct
    if (strcmp(pin, admin_pin) == 0)
    {
        admin_logged_in = true;

        failed_login_attempts = 0;
        lockout_until = 0;

        // Record when the administrator logged in
        last_admin_activity = current_time;

        ESP_LOGI(TAG, "Administrator login successful");

        return true;
    }

    // Incorrect PIN
    failed_login_attempts++;

    ESP_LOGW(TAG, "Incorrect PIN. Failed attempts: %d", failed_login_attempts);

    // Lock the system after three incorrect attempts
    if (failed_login_attempts >= MAX_LOGIN_ATTEMPTS)
    {
        lockout_until = current_time + LOCKOUT_DURATION_US;

        failed_login_attempts = 0;

        ESP_LOGW(TAG, "Too many failed attempts. Login locked for 60 seconds.");
    }

    return false;
}

// End the administrator session
void management_admin_logout(void)
{
    admin_logged_in = false;
    last_admin_activity = 0;

    ESP_LOGI(TAG, "Administrator logged out");
}

// Check whether an administrator is logged in

// Check whether the administrator session is still valid
bool management_admin_is_logged_in(void)
{
    if (!initialized || !admin_logged_in)
    {
        return false;
    }

    int64_t current_time = esp_timer_get_time();

    // Check whether the session has expired
    if ((current_time - last_admin_activity) >= SESSION_TIMEOUT_US)
    {
        admin_logged_in = false;

        ESP_LOGW(TAG, "Administrator session expired");

        return false;
    }

    return true;
}

// Refresh the administrator's activity timestamp
void management_admin_update_activity(void)
{
    if (management_admin_is_logged_in())
    {
        last_admin_activity = esp_timer_get_time();
    }
}

// Enable or disable emergency override
esp_err_t management_set_emergency_override(bool enable)
{
    // Only an authenticated administrator can change the override
    if (!management_admin_is_logged_in())
    {
        ESP_LOGW(TAG, "Access denied. Administrator login required.");
        return ESP_ERR_INVALID_STATE;
    }

    // Update emergency override state
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    emergency_override = enable;
    xSemaphoreGive(sched_lock);

    if (enable)
    {
        ESP_LOGW(TAG, "EMERGENCY OVERRIDE ACTIVATED");
    }
    else
    {
        ESP_LOGI(TAG, "Emergency override deactivated");
    }

    // Record administrator activity
    management_admin_update_activity();

    return ESP_OK;
}

// Display the current building management status
static void management_print_status(void)
{
    int open_h, open_m, close_h, close_m;
    bool override_on;

    // consistent snapshot while another task may be mid-configuration
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    open_h = opening_hour;
    open_m = opening_minute;
    close_h = closing_hour;
    close_m = closing_minute;
    override_on = emergency_override;
    xSemaphoreGive(sched_lock);

    printf("\n====================================\n");
    printf("       CoCIS BUILDING STATUS\n");
    printf("====================================\n");

    // Display configured opening and closing times
    printf("Opening Time: %02d:%02d\n", open_h, open_m);

    printf("Closing Time: %02d:%02d\n", close_h, close_m);

    // Check whether the building is currently open
    printf("Building Open Now: %s\n", management_is_open_now() ? "YES" : "NO");

    // Check administrator authentication
    printf("Administrator Logged In: %s\n", management_admin_is_logged_in() ? "YES" : "NO");

    // Check emergency override
    printf("Emergency Override: %s\n", override_on ? "ON" : "OFF");

    printf("====================================\n");

    // the application's occupancy/rooms section, if it registered one
    if (status_ext)
    {
        status_ext();
    }
}
// Display the configured building opening and closing times
static void management_print_schedule(void)
{
    int open_h, open_m, close_h, close_m;

    // consistent snapshot while another task may be mid-configuration
    xSemaphoreTake(sched_lock, portMAX_DELAY);
    open_h = opening_hour;
    open_m = opening_minute;
    close_h = closing_hour;
    close_m = closing_minute;
    xSemaphoreGive(sched_lock);

    printf("\n====================================\n");
    printf("       CoCIS BUILDING SCHEDULE\n");
    printf("====================================\n");

    printf("Opening Time: %02d:%02d\n", open_h, open_m);

    printf("Closing Time: %02d:%02d\n", close_h, close_m);

    printf("====================================\n");
}

// ---------------- Serial console (esp_console REPL) ----------------
// The REPL owns the terminal: line editing, echo, command history and the
// CoCIS> prompt all come from the esp_console component. Each command below
// is a small handler; the schedule and admin-session logic is untouched.

static int cmd_status(int argc, char **argv)
{
    management_print_status();
    return 0;
}

// login reads the PIN through the raw stream, printing one asterisk per
// character, so the PIN is never echoed and never lands in the REPL history
static int cmd_login(int argc, char **argv)
{
    if (management_admin_is_logged_in())
    {
        printf("An administrator is already logged in.\n");
        return 0;
    }

    printf("Enter administrator PIN: ");
    fflush(stdout);

    char pin[16];
    int pin_index = 0;

    while (1)
    {
        int ch = getchar();

        if (ch == EOF)
        {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\n' || ch == '\r')
        {
            break;
        }
        if ((ch == 0x7F || ch == '\b') && pin_index > 0)
        {
            pin_index--; // erase the last asterisk
            printf("\b \b");
            fflush(stdout);
        }
        else if (ch >= 0x20 && ch < 0x7F && pin_index < (int)sizeof(pin) - 1)
        {
            pin[pin_index++] = (char)ch;
            printf("*");
            fflush(stdout);
        }
    }
    pin[pin_index] = '\0';
    printf("\n");

    if (management_admin_login(pin))
    {
        printf("Administrator login successful.\n");
    }
    else
    {
        printf("Administrator login failed.\n");
    }

    memset(pin, 0, sizeof(pin));
    return 0;
}

static int cmd_logout(int argc, char **argv)
{
    if (management_admin_is_logged_in())
    {
        management_admin_logout();
        printf("Administrator logged out.\n");
    }
    else
    {
        printf("No administrator is currently logged in.\n");
    }
    return 0;
}

static int cmd_override(int argc, char **argv)
{
    bool enable;

    if (argc == 2 && strcmp(argv[1], "on") == 0)
    {
        enable = true;
    }
    else if (argc == 2 && strcmp(argv[1], "off") == 0)
    {
        enable = false;
    }
    else
    {
        printf("Usage: override on | off\n");
        return 1;
    }

    if (management_set_emergency_override(enable) == ESP_OK)
    {
        printf("Emergency override %s.\n", enable ? "activated" : "deactivated");
    }
    else
    {
        printf("Failed. Administrator login required.\n");
    }
    return 0;
}

static int cmd_schedule(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "show") == 0)
    {
        management_print_schedule();
        return 0;
    }

    if (argc == 4 && strcmp(argv[1], "set") == 0)
    {
        int open_h, open_m, close_h, close_m;

        if (sscanf(argv[2], "%d:%d", &open_h, &open_m) != 2 ||
            sscanf(argv[3], "%d:%d", &close_h, &close_m) != 2)
        {
            printf("Times must be HH:MM, e.g. schedule set 08:00 14:30\n");
            return 1;
        }

        if (management_set_schedule(open_h, open_m, close_h, close_m) == ESP_OK)
        {
            printf("Schedule updated.\n");
        }
        return 0;
    }

    printf("Usage: schedule show | schedule set <open HH:MM> <close HH:MM>\n");
    return 1;
}

// logs info | logs warn: runtime log discipline, so prints from other tasks
// stop repainting the terminal while commands are being typed
static int cmd_logs(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "info") == 0)
    {
        esp_log_level_set("*", ESP_LOG_INFO);
        printf("Runtime log level: INFO (verbose)\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "warn") == 0)
    {
        esp_log_level_set("*", ESP_LOG_WARN);
        printf("Runtime log level: WARN and above only\n");
        return 0;
    }
    printf("Usage: logs info | logs warn\n");
    return 1;
}

// occupancy building | occupancy rooms <id>, from the management_data seam
static int cmd_occupancy(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "building") == 0)
    {
        int count = 0;
        esp_err_t result = management_get_building_occupancy(&count);

        if (result == ESP_OK)
        {
            printf("Building occupancy: %d people inside\n", count);
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            printf("Building occupancy: no data yet.\n");
        }
        else
        {
            printf("Failed to retrieve building occupancy.\n");
        }
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "rooms") == 0)
    {
        int room_id = atoi(argv[2]);
        bool occupied = false;
        esp_err_t result = management_get_room_occupancy(room_id, &occupied);

        if (result == ESP_OK)
        {
            printf("Room %d: %s\n", room_id, occupied ? "OCCUPIED" : "UNOCCUPIED");
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            printf("Room %d: no such room is reporting.\n", room_id);
        }
        else
        {
            printf("Failed to retrieve room occupancy.\n");
        }
        return 0;
    }

    printf("Usage: occupancy building | occupancy rooms <id>\n");
    return 1;
}

static int cmd_lighting(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "status") == 0)
    {
        int room_id = atoi(argv[2]);
        bool light_on = false;
        esp_err_t result = management_get_lighting_status(room_id, &light_on);

        if (result == ESP_OK)
        {
            printf("Room %d lighting: %s\n", room_id, light_on ? "ON" : "OFF");
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            printf("Room %d: no such room is reporting.\n", room_id);
        }
        else
        {
            printf("Failed to retrieve lighting status.\n");
        }
        return 0;
    }

    printf("Usage: lighting status <id>\n");
    return 1;
}

// access records: admin-gated query through management_data
static int cmd_records(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "records") == 0)
    {
        if (!management_admin_is_logged_in())
        {
            printf("Access denied. Administrator login required.\n");
            return 0;
        }

        char records[640];
        esp_err_t result = management_get_access_records(records, sizeof(records));

        if (result == ESP_OK)
        {
            printf("\n=========== ACCESS RECORDS ===========\n");
            printf("%s", records);
            printf("\n=======================================\n");

            // viewing records keeps the admin session alive
            management_admin_update_activity();
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            printf("No access records yet.\n");
        }
        else
        {
            printf("Failed to retrieve access records.\n");
        }
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "records") == 0)
    {
        if (!management_admin_is_logged_in())
        {
            printf("Access denied. Administrator login required.\n");
            return 0;
        }

        char records[640];
        esp_err_t result = management_get_access_records_for_uid(argv[2], records, sizeof(records));

        if (result == ESP_OK)
        {
            printf("\n=========== ACCESS RECORDS (%s) ===========\n", argv[2]);
            printf("%s", records);
            printf("\n============================================\n");

            management_admin_update_activity();
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            printf("No access records for student %s yet.\n", argv[2]);
        }
        else
        {
            printf("Failed to retrieve access records for student %s.\n", argv[2]);
        }
        return 0;
    }

    printf("Usage: access records | access records <uid>\n");
    return 1;
}

esp_err_t management_start_cli(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "Management system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "CoCIS> ";
    repl_config.max_history_len = 10;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    static esp_console_repl_t *repl = NULL;
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    ESP_ERROR_CHECK(esp_console_register_help_command());

    const esp_console_cmd_t commands[] = {
        {.command = "status", .help = "Show building status", .func = &cmd_status},
        {.command = "login", .help = "Administrator login (PIN is masked)", .func = &cmd_login},
        {.command = "logout", .help = "Administrator logout", .func = &cmd_logout},
        {.command = "override", .help = "Emergency override", .hint = "on|off", .func = &cmd_override},
        {.command = "schedule", .help = "Building access schedule", .hint = "show | set <HH:MM> <HH:MM>", .func = &cmd_schedule},
        {.command = "logs", .help = "Runtime log level", .hint = "info|warn", .func = &cmd_logs},
        {.command = "occupancy", .help = "Occupancy query", .hint = "building | rooms <id>", .func = &cmd_occupancy},
        {.command = "lighting", .help = "Room lighting query", .hint = "status <id>", .func = &cmd_lighting},
        {.command = "access", .help = "Access history (admin)", .hint = "records | records <uid>", .func = &cmd_records},
    };

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    return ESP_OK;
}
