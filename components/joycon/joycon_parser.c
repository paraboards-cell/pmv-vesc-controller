#include "joycon_parser.h"
#include "joycon_types.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

#define TAG "joycon_parser"

// Joystick calibration — factory center/min/max from SPI flash (0x603D, 0x6050).
// These are reasonable defaults; real calibration data should be read via
// subcommand 0x10 (READ_SPI) at startup.
#define STICK_CENTER 2048
#define STICK_MIN    400
#define STICK_MAX    3700

static float calibrate_axis(uint16_t raw)
{
    float norm;
    if (raw >= STICK_CENTER) {
        norm = (float)(raw - STICK_CENTER) / (STICK_MAX - STICK_CENTER);
    } else {
        norm = (float)(raw - STICK_CENTER) / (STICK_CENTER - STICK_MIN);
    }
    // clamp to [-1, 1]
    if (norm >  1.0f) norm =  1.0f;
    if (norm < -1.0f) norm = -1.0f;
    return norm;
}

// Decode a 12-bit axis pair packed as 3 bytes little-endian (Joy-Con stick format)
static void decode_stick(const uint8_t *b, uint16_t *x, uint16_t *y)
{
    *x = b[0] | ((b[1] & 0x0F) << 8);
    *y = (b[1] >> 4) | (b[2] << 4);
}

// Parse 0x30 full input report (49 bytes including report ID)
// Report layout (after report ID byte):
//   [0]      timer
//   [1]      battery | connection info
//   [2..4]   button bytes (right, shared, left)
//   [5..7]   left stick (3 bytes, 12-bit packed)
//   [8..10]  right stick (3 bytes, 12-bit packed)
//   [11]     vibration ACK
//   [12..47] IMU data (3 samples x 6 int16 = 36 bytes)
static bool parse_full_report(const uint8_t *data, uint16_t len, joycon_state_t *out)
{
    if (len < 12) return false;

    const uint8_t *p = data;  // data[0] is already past the report ID byte

    out->battery_level = (p[1] >> 4) & 0x0F;

    // Button byte layout per dekuNukem reverse-engineering docs
    uint8_t btn_right  = p[2];
    uint8_t btn_shared = p[3];
    uint8_t btn_left   = p[4];

    // Right Joy-Con buttons
    out->btn_y  = btn_right & 0x01;
    out->btn_x  = btn_right & 0x02;
    out->btn_b  = btn_right & 0x04;
    out->btn_a  = btn_right & 0x08;
    out->btn_sr_right = btn_right & 0x10;
    out->btn_sl_right = btn_right & 0x20;
    out->btn_r  = btn_right & 0x40;
    out->btn_zr = btn_right & 0x80;

    // Shared buttons
    out->btn_minus   = btn_shared & 0x01;
    out->btn_plus    = btn_shared & 0x02;
    out->btn_rstick  = btn_shared & 0x04;
    out->btn_lstick  = btn_shared & 0x08;
    out->btn_home    = btn_shared & 0x10;
    out->btn_capture = btn_shared & 0x20;

    // Left Joy-Con buttons
    out->btn_down  = btn_left & 0x01;
    out->btn_up    = btn_left & 0x02;
    out->btn_right = btn_left & 0x04;
    out->btn_left  = btn_left & 0x08;
    out->btn_sr_left = btn_left & 0x10;
    out->btn_sl_left = btn_left & 0x20;
    out->btn_l     = btn_left & 0x40;
    out->btn_zl    = btn_left & 0x80;

    // Sticks
    uint16_t lx, ly, rx, ry;
    decode_stick(&p[5], &lx, &ly);
    decode_stick(&p[8], &rx, &ry);
    out->left_x  = calibrate_axis(lx);
    out->left_y  = calibrate_axis(ly);
    out->right_x = calibrate_axis(rx);
    out->right_y = calibrate_axis(ry);

    // IMU — first of three samples, 16-bit little-endian signed integers
    // Gyro: 0.06103°/s per LSB at 2000dps range
    // Accel: 0.000244g per LSB at 8g range
    if (len >= 48) {
        const uint8_t *imu = &p[12];
        int16_t ax = (int16_t)(imu[0] | (imu[1] << 8));
        int16_t ay = (int16_t)(imu[2] | (imu[3] << 8));
        int16_t az = (int16_t)(imu[4] | (imu[5] << 8));
        int16_t gx = (int16_t)(imu[6] | (imu[7] << 8));
        int16_t gy = (int16_t)(imu[8] | (imu[9] << 8));
        int16_t gz = (int16_t)(imu[10]| (imu[11] << 8));

        out->accel_x = ax * 0.000244f;
        out->accel_y = ay * 0.000244f;
        out->accel_z = az * 0.000244f;
        out->gyro_x  = gx * 0.06103f;
        out->gyro_y  = gy * 0.06103f;
        out->gyro_z  = gz * 0.06103f;
    }

    return true;
}

bool joycon_parse_report(const uint8_t *data, uint16_t len, joycon_state_t *out)
{
    if (len < 1) return false;

    uint8_t report_id = data[0];

    switch (report_id) {
    case JC_REPORT_FULL:
        // data+1 = past the report ID
        return parse_full_report(data + 1, len - 1, out);

    case JC_REPORT_SIMPLE:
        // 0x3F report means handshake hasn't completed yet — ignore
        ESP_LOGD(TAG, "received 0x3F simple report — handshake pending");
        return false;

    default:
        ESP_LOGD(TAG, "unhandled report ID 0x%02X (len=%u)", report_id, len);
        return false;
    }
}
