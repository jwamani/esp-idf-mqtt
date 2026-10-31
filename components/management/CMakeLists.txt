idf_component_register(
    SRCS "management.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
    PRIV_REQUIRES console
)