#pragma once
#include "joycon_types.h"
#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(JOYCON_EVENTS);

// Initialize Bluetooth Classic, register HID host, begin scanning.
// Callback fires with JOYCON_EVENT_* on connection/input.
esp_err_t joycon_bt_init(void);

// Stop scanning and disconnect any active Joy-Con.
esp_err_t joycon_bt_deinit(void);

// Send a subcommand to the connected Joy-Con.
// timer_byte should increment with each call (wraps at 0x0F).
esp_err_t joycon_send_subcommand(uint8_t subcmd, const uint8_t *args, uint8_t args_len);

// Get latest parsed state (thread-safe copy).
void joycon_get_state(joycon_state_t *out);
