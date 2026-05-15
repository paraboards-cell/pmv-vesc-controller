#pragma once
#include <stdint.h>
#include <stdbool.h>

// VESC UART packet command IDs (subset used by this project)
typedef enum {
    COMM_GET_VALUES        = 4,
    COMM_SET_DUTY          = 5,
    COMM_SET_CURRENT       = 6,
    COMM_SET_CURRENT_BRAKE = 7,
    COMM_SET_RPM           = 8,
    COMM_SET_HANDBRAKE     = 68,
    COMM_FORWARD_CAN       = 33,
    COMM_GET_VALUES_SETUP  = 50,
} vesc_comm_id_t;

typedef struct {
    float    temp_mosfet;       // °C
    float    temp_motor;        // °C
    float    avg_motor_current; // A
    float    avg_input_current; // A
    float    duty_cycle;        // 0.0–1.0
    int32_t  rpm;
    float    input_voltage;     // V
    float    amp_hours;
    float    watt_hours;
    uint8_t  fault_code;
} vesc_values_t;
