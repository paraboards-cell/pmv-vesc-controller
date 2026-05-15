#pragma once
#include <stdint.h>
#include <stdbool.h>

// Joy-Con input report IDs
#define JC_REPORT_SIMPLE    0x3F   // button-only, received before handshake
#define JC_REPORT_FULL      0x30   // full report with sticks + IMU (post-handshake)

// Joy-Con output report ID (sent to device)
#define JC_OUTPUT_RUMBLE_SUBCMD 0x01

// Joy-Con subcommand IDs
#define JC_SUBCMD_SET_INPUT_MODE    0x03
#define JC_SUBCMD_READ_SPI          0x10
#define JC_SUBCMD_SET_PLAYER_LIGHTS 0x30
#define JC_SUBCMD_ENABLE_IMU        0x40
#define JC_SUBCMD_ENABLE_VIBRATION  0x48

// Input mode for full 0x30 reports
#define JC_INPUT_MODE_FULL          0x30

// ─── Parsed state ─────────────────────────────────────────────────────────────

typedef struct {
    // Buttons (Right Joy-Con layout — Right JC only, else 0)
    bool btn_y, btn_x, btn_a, btn_b;
    bool btn_r, btn_zr;
    bool btn_sr_right, btn_sl_right;

    // Buttons (Left Joy-Con layout — Left JC only, else 0)
    bool btn_up, btn_down, btn_left, btn_right;
    bool btn_l, btn_zl;
    bool btn_sr_left, btn_sl_left;

    // Shared
    bool btn_minus, btn_plus;
    bool btn_lstick, btn_rstick;
    bool btn_home, btn_capture;

    // Joystick axes — calibrated, range [-1.0, +1.0]
    float left_x,  left_y;
    float right_x, right_y;

    // IMU (°/s and g, unfiltered) — valid when IMU enabled
    float gyro_x,  gyro_y,  gyro_z;
    float accel_x, accel_y, accel_z;

    uint8_t battery_level;  // 0–8 (8=full)
    bool    connected;
} joycon_state_t;

// Connection event types posted to default event loop
typedef enum {
    JOYCON_EVENT_CONNECTED,
    JOYCON_EVENT_DISCONNECTED,
    JOYCON_EVENT_INPUT,       // data = joycon_state_t*
} joycon_event_t;
