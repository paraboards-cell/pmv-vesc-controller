idf_component_register(
    SRCS "vesc_uart.c" "vesc_packet.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
