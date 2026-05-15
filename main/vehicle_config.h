#pragma once

// ─── Hardware pins ────────────────────────────────────────────────────────────
#define VESC_UART_TX_PIN        17
#define VESC_UART_RX_PIN        16
#define VESC_UART_NUM           UART_NUM_1
#define VESC_BAUD_RATE          115200

#define CASTOR_GPIO_PIN         18   // active-high: HIGH = castor down (rear lifted)

// ─── VESC CAN IDs ─────────────────────────────────────────────────────────────
// Master VESC is addressed directly via UART.
// Slave is addressed via COMM_FORWARD_CAN with its controller ID.
#define VESC_MASTER_ID          0    // left front motor (direct UART)
#define VESC_SLAVE_CAN_ID       1    // right front motor (forwarded over CAN)

// ─── Motor direction ──────────────────────────────────────────────────────────
// Flip if a motor runs backwards after FOC motor detection.
#define MOTOR_LEFT_INVERT       false
#define MOTOR_RIGHT_INVERT      false

// ─── Speed modes (motor current as fraction of max, 0.0–1.0) ──────────────────
#define SPEED_MODE_WALK_FRAC    0.25f   // ~3 km/h
#define SPEED_MODE_CRUISE_FRAC  0.60f   // ~15 km/h
#define SPEED_MODE_SPORT_FRAC   1.00f   // full current

// ─── Motor current limits (Amps) ──────────────────────────────────────────────
// Set these to your motor's rated continuous current after VESC detection.
#define MOTOR_MAX_CURRENT_A     40.0f
#define MOTOR_BRAKE_CURRENT_A   20.0f
#define MOTOR_HANDBRAKE_CURRENT_A 15.0f

// ─── Drive mixing ─────────────────────────────────────────────────────────────
// Max fraction of throttle that steering can subtract/add (tank mix).
// 1.0 = counter-steer is possible (zero-radius spin at full stick).
#define STEER_MIX_RATIO         0.8f

// Joystick dead-band (fraction of full-scale, applied before expo).
#define THROTTLE_DEADBAND       0.05f
#define STEER_DEADBAND          0.05f

// Expo curve factor [0.0 = linear, 1.0 = full cubic].
#define THROTTLE_EXPO           0.30f
#define STEER_EXPO              0.40f

// ─── Castor control ───────────────────────────────────────────────────────────
// Castor drops (rear lifts) when speed is below threshold AND steer > threshold.
#define CASTOR_SPEED_THRESHOLD_RPM   200    // below this = slow speed
#define CASTOR_STEER_THRESHOLD       0.75f  // steer axis fraction
#define CASTOR_HYSTERESIS_RPM        50     // prevents chatter

// ─── Safety watchdog ──────────────────────────────────────────────────────────
// If no valid Joy-Con input is received within this window, motors are stopped.
#define WATCHDOG_TIMEOUT_MS     500

// ─── VESC telemetry poll rate ─────────────────────────────────────────────────
#define TELEMETRY_POLL_MS       100
