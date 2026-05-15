#pragma once
#include "vesc_types.h"
#include "driver/uart.h"
#include <esp_err.h>

// Initialize VESC UART driver. Call once from app_main.
esp_err_t vesc_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud);

// ─── Motor commands ───────────────────────────────────────────────────────────

// Set motor current in Amps. Positive = forward, negative = reverse (regen).
// For dual-VESC: left is on master (can_id ignored for master),
//               right is forwarded via CAN to slave.
esp_err_t vesc_set_current(uint8_t can_id, float current_a, bool is_slave);

// Set braking current (positive value = braking force).
esp_err_t vesc_set_current_brake(uint8_t can_id, float brake_a, bool is_slave);

// Engage parking brake by holding position with current.
esp_err_t vesc_set_handbrake(uint8_t can_id, float current_a, bool is_slave);

// Release all current — motor freewheels. Use for E-stop freewheeling.
esp_err_t vesc_set_current_zero(uint8_t can_id, bool is_slave);

// ─── Telemetry ────────────────────────────────────────────────────────────────

// Read telemetry from master VESC (direct UART).
esp_err_t vesc_get_values(vesc_values_t *out);

// Read telemetry from slave VESC via CAN forwarding.
// The master forwards the request and returns the slave's response on UART.
esp_err_t vesc_get_values_slave(uint8_t can_id, vesc_values_t *out);
