#include "joycon_calibration.h"
#include "joycon_bt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "joycon_cal"

// Factory SPI addresses (dekuNukem/Nintendo_Switch_Reverse_Engineering)
#define SPI_ADDR_LEFT_CAL   0x603D   // 9 bytes: max_delta, center, min_delta
#define SPI_ADDR_RIGHT_CAL  0x6046   // 9 bytes: center, min_delta, max_delta

// Compile-time fallback — reasonable mid-range defaults
#define DEFAULT_CENTER  2048
#define DEFAULT_MIN      400
#define DEFAULT_MAX     3700

static SemaphoreHandle_t s_reply_sem = NULL;
static uint8_t           s_reply_buf[64];
static uint16_t          s_reply_len = 0;

// ─── Decode a 9-byte stick calibration block ──────────────────────────────────
// Each block encodes 3 (x,y) pairs, each axis 12-bit little-endian packed:
//   x = byte[0] | ((byte[1] & 0x0F) << 8)
//   y = (byte[1] >> 4) | (byte[2] << 4)

static void decode_cal_pair(const uint8_t *b, uint16_t *x, uint16_t *y)
{
    *x = b[0] | ((b[1] & 0x0F) << 8);
    *y = (b[1] >> 4) | ((uint16_t)b[2] << 4);
}

// Left stick factory layout: [max_delta][center][min_delta]
static bool decode_left(const uint8_t *data, stick_cal_t *out)
{
    uint16_t max_dx, max_dy, cx, cy, min_dx, min_dy;
    decode_cal_pair(&data[0], &max_dx, &max_dy);
    decode_cal_pair(&data[3], &cx,     &cy);
    decode_cal_pair(&data[6], &min_dx, &min_dy);

    if (cx < 100 || cx > 4000 || cy < 100 || cy > 4000) return false;

    out->x_center = cx;
    out->y_center = cy;
    out->x_max    = cx + max_dx;
    out->y_max    = cy + max_dy;
    out->x_min    = (cx > min_dx) ? cx - min_dx : 0;
    out->y_min    = (cy > min_dy) ? cy - min_dy : 0;
    return true;
}

// Right stick factory layout: [center][min_delta][max_delta]
static bool decode_right(const uint8_t *data, stick_cal_t *out)
{
    uint16_t cx, cy, min_dx, min_dy, max_dx, max_dy;
    decode_cal_pair(&data[0], &cx,     &cy);
    decode_cal_pair(&data[3], &min_dx, &min_dy);
    decode_cal_pair(&data[6], &max_dx, &max_dy);

    if (cx < 100 || cx > 4000 || cy < 100 || cy > 4000) return false;

    out->x_center = cx;
    out->y_center = cy;
    out->x_max    = cx + max_dx;
    out->y_max    = cy + max_dy;
    out->x_min    = (cx > min_dx) ? cx - min_dx : 0;
    out->y_min    = (cy > min_dy) ? cy - min_dy : 0;
    return true;
}

// ─── SPI read via subcommand 0x10 ────────────────────────────────────────────

static bool read_spi(uint16_t addr, uint8_t length, uint8_t *data_out,
                     uint32_t timeout_ms)
{
    uint8_t args[5] = {
        addr & 0xFF,
        (addr >> 8) & 0xFF,
        0x00, 0x00,
        length
    };
    joycon_send_subcommand(0x10, args, sizeof(args));

    if (xSemaphoreTake(s_reply_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "SPI read timeout (addr=0x%04X)", addr);
        return false;
    }

    // 0x21 reply layout: [report_id][timer][batt][btns x3][sticks x6][vib]
    //   [13]=ACK [14]=subcmd_id [15-18]=addr [19]=len [20..]=data
    if (s_reply_len < 21) return false;
    uint8_t ack = s_reply_buf[13];
    if ((ack & 0x80) == 0) {
        ESP_LOGW(TAG, "SPI NACK (addr=0x%04X ack=0x%02X)", addr, ack);
        return false;
    }
    memcpy(data_out, &s_reply_buf[20], length);
    return true;
}

static void apply_defaults(stick_cal_t *s)
{
    s->x_center = s->y_center = DEFAULT_CENTER;
    s->x_min    = s->y_min    = DEFAULT_MIN;
    s->x_max    = s->y_max    = DEFAULT_MAX;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool joycon_cal_read(joycon_cal_t *out, uint32_t timeout_ms)
{
    if (!s_reply_sem) {
        s_reply_sem = xSemaphoreCreateBinary();
    }

    uint8_t raw[9];
    bool ok = true;

    if (read_spi(SPI_ADDR_LEFT_CAL, 9, raw, timeout_ms) &&
        decode_left(raw, &out->left)) {
        ESP_LOGI(TAG, "left  cal: center(%u,%u) min(%u,%u) max(%u,%u)",
                 out->left.x_center,  out->left.y_center,
                 out->left.x_min,     out->left.y_min,
                 out->left.x_max,     out->left.y_max);
    } else {
        ESP_LOGW(TAG, "left stick cal failed — using defaults");
        apply_defaults(&out->left);
        ok = false;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    if (read_spi(SPI_ADDR_RIGHT_CAL, 9, raw, timeout_ms) &&
        decode_right(raw, &out->right)) {
        ESP_LOGI(TAG, "right cal: center(%u,%u) min(%u,%u) max(%u,%u)",
                 out->right.x_center, out->right.y_center,
                 out->right.x_min,    out->right.y_min,
                 out->right.x_max,    out->right.y_max);
    } else {
        ESP_LOGW(TAG, "right stick cal failed — using defaults");
        apply_defaults(&out->right);
        ok = false;
    }

    out->valid = ok;
    return ok;
}

void joycon_cal_on_reply(const uint8_t *data, uint16_t len)
{
    if (!s_reply_sem) return;
    uint16_t copy_len = len < sizeof(s_reply_buf) ? len : sizeof(s_reply_buf);
    memcpy(s_reply_buf, data, copy_len);
    s_reply_len = copy_len;
    xSemaphoreGiveFromISR(s_reply_sem, NULL);
}

float joycon_cal_axis(uint16_t raw, uint16_t center, uint16_t min, uint16_t max)
{
    float v;
    if (raw >= center) {
        v = (max == center) ? 0.0f : (float)(raw - center) / (float)(max - center);
    } else {
        v = (center == min) ? 0.0f : -(float)(center - raw) / (float)(center - min);
    }
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}
