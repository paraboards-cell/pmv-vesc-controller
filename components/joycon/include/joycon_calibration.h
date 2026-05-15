#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x_center;
    uint16_t y_center;
    uint16_t x_max;   // center + max_delta
    uint16_t y_max;
    uint16_t x_min;   // center - min_delta
    uint16_t y_min;
} stick_cal_t;

typedef struct {
    stick_cal_t left;
    stick_cal_t right;
    bool valid;   // false = using compile-time defaults
} joycon_cal_t;

// Read factory stick calibration from Joy-Con SPI flash.
// Blocks up to timeout_ms waiting for the subcommand reply.
// Returns true and populates *out on success; false uses defaults.
bool joycon_cal_read(joycon_cal_t *out, uint32_t timeout_ms);

// Notify calibration module that a 0x21 subcommand reply arrived.
// Called from the HID input event handler.
void joycon_cal_on_reply(const uint8_t *data, uint16_t len);

// Apply calibration to a raw 12-bit axis value → [-1.0, +1.0]
float joycon_cal_axis(uint16_t raw, uint16_t center, uint16_t min, uint16_t max);
