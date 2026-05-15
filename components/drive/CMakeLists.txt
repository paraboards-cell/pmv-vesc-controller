idf_component_register(
    SRCS "drive_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES vesc joycon esp_event
)
