idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES vesc joycon drive castor esp_event nvs_flash
)
