idf_component_register(
    SRCS "net.c" "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash
)
