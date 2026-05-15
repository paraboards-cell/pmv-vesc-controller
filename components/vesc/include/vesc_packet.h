#pragma once
#include "vesc_types.h"
#include <stdint.h>
#include <stdbool.h>

// Encode payload into a VESC UART packet. Returns total packet byte count or -1.
int  vesc_packet_encode(const uint8_t *payload, uint16_t payload_len,
                        uint8_t *out, uint16_t out_max);

// Payload builders — write into buf, set *len_out to payload byte count
void vesc_payload_set_current(uint8_t *buf, uint8_t *len_out, float current_a);
void vesc_payload_set_current_brake(uint8_t *buf, uint8_t *len_out, float brake_a);
void vesc_payload_set_handbrake(uint8_t *buf, uint8_t *len_out, float current_a);
void vesc_payload_get_values(uint8_t *buf, uint8_t *len_out);

// Wrap inner_payload for CAN-forwarded slave VESC
void vesc_payload_forward_can(uint8_t *buf, uint8_t *len_out,
                               uint8_t can_id,
                               const uint8_t *inner_payload, uint8_t inner_len);

// Parse COMM_GET_VALUES response payload into vesc_values_t. Returns false on error.
bool vesc_response_parse_values(const uint8_t *payload, uint16_t len,
                                 vesc_values_t *out);
