#include "vesc_packet.h"
#include <string.h>

// CRC16-CCITT (poly 0x1021, init 0x0000) — matches VESC firmware
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

int vesc_packet_encode(const uint8_t *payload, uint16_t payload_len,
                       uint8_t *out, uint16_t out_max)
{
    // Short packet: start=0x02, 1-byte length, payload, CRC16 x2, end=0x03
    if (payload_len > 256 || (payload_len + 5) > out_max) return -1;

    uint16_t crc = crc16(payload, payload_len);
    int idx = 0;
    out[idx++] = 0x02;
    out[idx++] = (uint8_t)payload_len;
    memcpy(&out[idx], payload, payload_len);
    idx += payload_len;
    out[idx++] = (crc >> 8) & 0xFF;
    out[idx++] = crc & 0xFF;
    out[idx++] = 0x03;
    return idx;
}

// ─── Payload helpers ──────────────────────────────────────────────────────────

static inline void pack_float32(uint8_t *buf, float v, float scale)
{
    int32_t i = (int32_t)(v * scale);
    buf[0] = (i >> 24) & 0xFF;
    buf[1] = (i >> 16) & 0xFF;
    buf[2] = (i >> 8)  & 0xFF;
    buf[3] =  i        & 0xFF;
}

static inline void pack_int32(uint8_t *buf, int32_t v)
{
    buf[0] = (v >> 24) & 0xFF;
    buf[1] = (v >> 16) & 0xFF;
    buf[2] = (v >> 8)  & 0xFF;
    buf[3] =  v        & 0xFF;
}

void vesc_payload_set_current(uint8_t *buf, uint8_t *len_out, float current_a)
{
    buf[0] = COMM_SET_CURRENT;
    pack_float32(&buf[1], current_a, 1000.0f);
    *len_out = 5;
}

void vesc_payload_set_current_brake(uint8_t *buf, uint8_t *len_out, float brake_a)
{
    buf[0] = COMM_SET_CURRENT_BRAKE;
    pack_float32(&buf[1], brake_a, 1000.0f);
    *len_out = 5;
}

void vesc_payload_set_handbrake(uint8_t *buf, uint8_t *len_out, float current_a)
{
    buf[0] = COMM_SET_HANDBRAKE;
    pack_float32(&buf[1], current_a, 1000.0f);
    *len_out = 5;
}

void vesc_payload_forward_can(uint8_t *buf, uint8_t *len_out,
                               uint8_t can_id,
                               const uint8_t *inner_payload, uint8_t inner_len)
{
    buf[0] = COMM_FORWARD_CAN;
    buf[1] = can_id;
    memcpy(&buf[2], inner_payload, inner_len);
    *len_out = 2 + inner_len;
}

void vesc_payload_get_values(uint8_t *buf, uint8_t *len_out)
{
    buf[0] = COMM_GET_VALUES;
    *len_out = 1;
}

// ─── Response parsing ─────────────────────────────────────────────────────────

static inline float unpack_float16(const uint8_t *b, float scale)
{
    int16_t v = ((int16_t)b[0] << 8) | b[1];
    return (float)v / scale;
}

static inline float unpack_float32(const uint8_t *b, float scale)
{
    int32_t v = ((int32_t)b[0] << 24) | ((int32_t)b[1] << 16) |
                ((int32_t)b[2] << 8)  |  b[3];
    return (float)v / scale;
}

static inline int32_t unpack_int32(const uint8_t *b)
{
    return ((int32_t)b[0] << 24) | ((int32_t)b[1] << 16) |
           ((int32_t)b[2] << 8)  |  b[3];
}

bool vesc_response_parse_values(const uint8_t *payload, uint16_t len,
                                 vesc_values_t *out)
{
    // Minimum COMM_GET_VALUES response payload length
    if (len < 55 || payload[0] != COMM_GET_VALUES) return false;

    const uint8_t *p = &payload[1];
    out->temp_mosfet       = unpack_float16(p + 0,  10.0f);
    out->temp_motor        = unpack_float16(p + 2,  10.0f);
    out->avg_motor_current = unpack_float32(p + 4,  100.0f);
    out->avg_input_current = unpack_float32(p + 8,  100.0f);
    // bytes 12-15: avg_id, bytes 16-19: avg_iq (skip)
    out->duty_cycle        = unpack_float16(p + 20, 1000.0f);
    out->rpm               = unpack_int32(p + 22);
    out->input_voltage     = unpack_float16(p + 26, 10.0f);
    out->amp_hours         = unpack_float32(p + 28, 10000.0f);
    // amp_hours_charged at p+32 (skip)
    out->watt_hours        = unpack_float32(p + 36, 10000.0f);
    // watt_hours_charged at p+40 (skip)
    out->fault_code        = p[54];
    return true;
}
