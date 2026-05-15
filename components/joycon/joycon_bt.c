#include "joycon_bt.h"
#include "joycon_parser.h"
#include "joycon_calibration.h"
#include "esp_hidh.h"
#include "esp_hid_common.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "joycon_bt"

// Nintendo Joy-Con vendor/product IDs for device filtering
#define JC_VID 0x057E
#define JC_PID_LEFT  0x2006
#define JC_PID_RIGHT 0x2007
#define JC_PID_PRO   0x2009

ESP_EVENT_DEFINE_BASE(JOYCON_EVENTS);

static esp_hidh_dev_t  *s_dev       = NULL;
static SemaphoreHandle_t s_state_mx = NULL;
static joycon_state_t    s_state    = {0};
static uint8_t           s_timer    = 0;
static joycon_cal_t      s_cal      = {0};

// ─── Subcommand helpers ───────────────────────────────────────────────────────

static const uint8_t RUMBLE_NEUTRAL[8] = {
    0x00, 0x01, 0x40, 0x40,
    0x00, 0x01, 0x40, 0x40
};

esp_err_t joycon_send_subcommand(uint8_t subcmd, const uint8_t *args, uint8_t args_len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t buf[64] = {0};
    buf[0] = s_timer++ & 0x0F;
    memcpy(&buf[1], RUMBLE_NEUTRAL, 8);
    buf[9] = subcmd;
    if (args && args_len) {
        memcpy(&buf[10], args, args_len);
    }

    // Output report 0x01 = Rumble + Subcommand
    esp_err_t err = esp_hidh_dev_report_send(s_dev,
                                              ESP_HID_TRANSPORT_BT,
                                              ESP_HID_REPORT_TYPE_OUTPUT,
                                              JC_OUTPUT_RUMBLE_SUBCMD,
                                              buf, 10 + args_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "subcommand 0x%02X failed: %s", subcmd, esp_err_to_name(err));
    }
    return err;
}

// ─── Handshake sequence ───────────────────────────────────────────────────────
// After connection, Joy-Con sends 0x3F simple reports. We must:
//   1. Enable vibration
//   2. Set full input mode (0x30)
//   3. Enable IMU
// Without this sequence the stick data is not included in reports.

static void run_handshake(void)
{
    vTaskDelay(pdMS_TO_TICKS(150));   // give device time to settle after connect

    uint8_t arg;

    arg = 0x01;  // enable vibration
    joycon_send_subcommand(JC_SUBCMD_ENABLE_VIBRATION, &arg, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    arg = JC_INPUT_MODE_FULL;  // switch to 0x30 full reports
    joycon_send_subcommand(JC_SUBCMD_SET_INPUT_MODE, &arg, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    arg = 0x01;  // enable 6-axis IMU
    joycon_send_subcommand(JC_SUBCMD_ENABLE_IMU, &arg, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set player light 1 to identify which controller this is
    arg = 0x01;
    joycon_send_subcommand(JC_SUBCMD_SET_PLAYER_LIGHTS, &arg, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read factory stick calibration from SPI flash
    joycon_cal_read(&s_cal, 300);
    joycon_parser_set_cal(&s_cal);

    ESP_LOGI(TAG, "handshake complete — expecting 0x30 reports");
}

// ─── HID host event callback ──────────────────────────────────────────────────

static void hidh_callback(void *handler_arg, esp_event_base_t base,
                           int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *data = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        if (data->open.status != ESP_OK) {
            ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(data->open.status));
            break;
        }
        s_dev = data->open.dev;
        ESP_LOGI(TAG, "Joy-Con connected: %s", esp_hidh_dev_name(s_dev));

        esp_event_post(JOYCON_EVENTS, JOYCON_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);

        // Run handshake in a short-lived task so we don't block the event loop
        xTaskCreate((TaskFunction_t)run_handshake, "jc_handshake", 3072, NULL, 5, NULL);
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        if (data->input.length < 1) break;

        // 0x21 = subcommand reply (used during calibration SPI reads)
        if (data->input.data[0] == 0x21) {
            joycon_cal_on_reply(data->input.data, data->input.length);
            break;
        }

        joycon_state_t new_state = {0};
        bool parsed = joycon_parse_report(data->input.data, data->input.length, &new_state);

        if (parsed) {
            xSemaphoreTake(s_state_mx, portMAX_DELAY);
            memcpy(&s_state, &new_state, sizeof(s_state));
            s_state.connected = true;
            xSemaphoreGive(s_state_mx);

            esp_event_post(JOYCON_EVENTS, JOYCON_EVENT_INPUT,
                           &s_state, sizeof(s_state), portMAX_DELAY);
        }
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
        ESP_LOGW(TAG, "Joy-Con disconnected");
        s_dev = NULL;
        xSemaphoreTake(s_state_mx, portMAX_DELAY);
        memset(&s_state, 0, sizeof(s_state));
        xSemaphoreGive(s_state_mx);
        esp_event_post(JOYCON_EVENTS, JOYCON_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
        break;
    }
    default: break;
    }
}

// ─── GAP scan callback — filter for Joy-Con, auto-connect ────────────────────

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event != ESP_BT_GAP_DISC_RES_EVT) return;

    // Walk EIR records looking for COD=peripheral or Joy-Con name
    uint8_t *eir = param->disc_res.p_sr;
    if (!eir) return;

    char name[64] = {0};
    uint8_t name_len = 0;
    uint8_t *name_data = esp_bt_gap_resolve_eir_data(eir,
                             ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &name_len);
    if (!name_data) {
        name_data = esp_bt_gap_resolve_eir_data(eir,
                        ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &name_len);
    }
    if (name_data && name_len < sizeof(name)) {
        memcpy(name, name_data, name_len);
    }

    if (strstr(name, "Joy-Con") || strstr(name, "Pro Controller")) {
        ESP_LOGI(TAG, "Found: %s — connecting", name);
        esp_bt_gap_cancel_discovery();
        esp_hidh_dev_open(param->disc_res.bda, ESP_HID_TRANSPORT_BT, 0);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t joycon_bt_init(void)
{
    s_state_mx = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    const esp_hidh_config_t hidh_cfg = {
        .callback       = hidh_callback,
        .event_stack_size = 4096,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
    ESP_ERROR_CHECK(esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0));

    ESP_LOGI(TAG, "scanning for Joy-Con...");
    return ESP_OK;
}

esp_err_t joycon_bt_deinit(void)
{
    esp_bt_gap_cancel_discovery();
    if (s_dev) esp_hidh_dev_close(s_dev);
    return esp_hidh_deinit();
}

void joycon_get_state(joycon_state_t *out)
{
    xSemaphoreTake(s_state_mx, portMAX_DELAY);
    memcpy(out, &s_state, sizeof(s_state));
    xSemaphoreGive(s_state_mx);
}
