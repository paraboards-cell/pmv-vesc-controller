idf_component_register(
    SRCS "joycon_bt.c" "joycon_parser.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_event
)
