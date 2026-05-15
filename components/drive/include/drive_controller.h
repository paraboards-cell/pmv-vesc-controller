#pragma once
#include "joycon_types.h"
#include <stdbool.h>

typedef enum {
    DRIVE_MODE_WALK   = 0,  // low current, creep speed
    DRIVE_MODE_CRUISE = 1,  // mid current
    DRIVE_MODE_SPORT  = 2,  // full current
} drive_mode_t;

typedef enum {
    DRIVE_STATE_STOPPED,
    DRIVE_STATE_RUNNING,
    DRIVE_STATE_BRAKING,
    DRIVE_STATE_PARKING_BRAKE,
    DRIVE_STATE_FAULT,
} drive_state_t;

typedef struct {
    float left_current_a;   // commanded current for left motor
    float right_current_a;  // commanded current for right motor
    drive_state_t state;
    drive_mode_t  mode;
    bool          castor_active;
} drive_output_t;

// Initialize drive controller. Must be called after vesc_uart_init().
void drive_controller_init(void);

// Process one Joy-Con input frame. Returns the commanded motor outputs.
// Also sends commands to VESC directly.
drive_output_t drive_update(const joycon_state_t *input);

// Force-stop both motors (current = 0). Use for watchdog timeout.
void drive_estop(void);

// Engage / release parking brake.
void drive_parking_brake(bool engage);

// Change speed mode.
void drive_set_mode(drive_mode_t mode);
drive_mode_t drive_get_mode(void);
