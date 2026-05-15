#include "vehicle_config.h"
#include "joycon_bt.h"
#include "joycon_types.h"
#include "drive_controller.h"
#include "vesc_uart.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define TAG "main"

// ─── Watchdog ─────────────────────────────────────────────────────────────────

static TimerHandle_t s_watchdog_timer = NULL;

static void watchdog_expired(TimerHandle_t t)
{
    ESP_LOGW(TAG, "Joy-Con input timeout — E-STOP");
    drive_estop();
}

static void watchdog_feed(void)
{
    xTimerReset(s_watchdog_timer, 0);
}

// ─── Joy-Con event handler ────────────────────────────────────────────────────

static void on_joycon_event(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    switch ((joycon_event_t)id) {
    case JOYCON_EVENT_CONNECTED:
        ESP_LOGI(TAG, "controller connected");
        xTimerStart(s_watchdog_timer, 0);
        break;

    case JOYCON_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "controller disconnected");
        xTimerStop(s_watchdog_timer, 0);
        drive_estop();
        break;

    case JOYCON_EVENT_INPUT: {
        const joycon_state_t *state = (const joycon_state_t *)data;
        watchdog_feed();

        // Minus button = toggle parking brake
        static bool last_minus = false;
        static bool parked = false;
        if (state->btn_minus && !last_minus) {
            parked = !parked;
            drive_parking_brake(parked);
        }
        last_minus = state->btn_minus;

        if (!parked) {
            drive_update(state);
        }
        break;
    }
    }
}

// ─── Telemetry task ───────────────────────────────────────────────────────────

static void log_vesc(const char *label, const vesc_values_t *v)
{
    ESP_LOGI(TAG, "%-6s | RPM:%6ld  I:%5.1fA  V:%4.1fV  T_fet:%4.0fC  T_mot:%4.0fC  fault:%u",
             label, (long)v->rpm, v->avg_motor_current, v->input_voltage,
             v->temp_mosfet, v->temp_motor, v->fault_code);
}

static void telemetry_task(void *arg)
{
    vesc_values_t left = {0}, right = {0};
    while (1) {
        bool fault = false;

        if (vesc_get_values(&left) == ESP_OK) {
            log_vesc("LEFT", &left);
            if (left.fault_code != 0) {
                ESP_LOGE(TAG, "LEFT VESC FAULT %u", left.fault_code);
                fault = true;
            }
        } else {
            ESP_LOGW(TAG, "LEFT VESC telemetry timeout");
        }

        // Small gap between requests — master needs time to process
        vTaskDelay(pdMS_TO_TICKS(20));

        if (vesc_get_values_slave(VESC_SLAVE_CAN_ID, &right) == ESP_OK) {
            log_vesc("RIGHT", &right);
            if (right.fault_code != 0) {
                ESP_LOGE(TAG, "RIGHT VESC FAULT %u", right.fault_code);
                fault = true;
            }
        } else {
            ESP_LOGW(TAG, "RIGHT VESC telemetry timeout");
        }

        if (fault) {
            drive_estop();
        }

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_POLL_MS));
    }
}

// ─── Entry point ──────────────────────────────────────────────────────────────

void app_main(void)
{
    // NVS is required by Bluetooth stack for bonding/pairing keys
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // VESC UART
    ESP_ERROR_CHECK(vesc_uart_init(VESC_UART_NUM, VESC_UART_TX_PIN,
                                   VESC_UART_RX_PIN, VESC_BAUD_RATE));

    // Drive controller (also inits castor GPIO)
    drive_controller_init();

    // Watchdog timer — resets on every Joy-Con input event
    s_watchdog_timer = xTimerCreate("jc_wd", pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS),
                                    pdFALSE, NULL, watchdog_expired);

    // Register for Joy-Con events
    ESP_ERROR_CHECK(esp_event_handler_register(JOYCON_EVENTS, ESP_EVENT_ANY_ID,
                                               on_joycon_event, NULL));

    // Start Bluetooth + Joy-Con HID host
    ESP_ERROR_CHECK(joycon_bt_init());

    // Telemetry polling task
    xTaskCreate(telemetry_task, "telemetry", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "PMV controller ready — waiting for Joy-Con");
}
