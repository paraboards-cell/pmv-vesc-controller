#include "castor_control.h"
#include "vehicle_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "castor"

static bool s_active = false;

void castor_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CASTOR_GPIO_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(CASTOR_GPIO_PIN, 0);
    ESP_LOGI(TAG, "initialized on GPIO %d", CASTOR_GPIO_PIN);
}

void castor_set(bool active)
{
    if (active == s_active) return;
    s_active = active;
    gpio_set_level(CASTOR_GPIO_PIN, active ? 1 : 0);
    ESP_LOGD(TAG, "%s", active ? "DOWN (rear lifted)" : "UP");
}

bool castor_is_active(void) { return s_active; }
