idf_component_register(
    SRCS "hub_status.c" "net.c" "app_main.c" "rooms.c" "building.c"
         "access_ctrl.c" "occupancy.c" "door_ctrl.c" "lcd_i2c.c"
         "rfid_reader.c" "management_bridge.c" "management.c"
         "management_data.c" "storage.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash
                  esp_driver_gpio esp_driver_spi esp_driver_i2c
                  esp_driver_ledc esp_timer
                  fatfs sdmmc esp_driver_sdspi
                  console
)
