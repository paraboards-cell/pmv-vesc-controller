#include "vesc_uart.h"
#include "vesc_packet.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

#define TAG "vesc"
#define BUF_SIZE 512
#define RX_TIMEOUT_MS 100

static uart_port_t s_port;

esp_err_t vesc_uart_init(uart_port_t port, int tx_pin, int rx_pin, int baud)
{
    s_port = port;
    const uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    return uart_driver_install(port, BUF_SIZE * 2, 0, 0, NULL, 0);
}

static esp_err_t send_packet(const uint8_t *payload, uint8_t payload_len)
{
    uint8_t pkt[BUF_SIZE];
    int pkt_len = vesc_packet_encode(payload, payload_len, pkt, sizeof(pkt));
    if (pkt_len < 0) return ESP_ERR_INVALID_SIZE;
    int written = uart_write_bytes(s_port, pkt, pkt_len);
    return (written == pkt_len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_to_slave(uint8_t can_id,
                                const uint8_t *inner_payload, uint8_t inner_len)
{
    uint8_t fwd_payload[64];
    uint8_t fwd_len;
    vesc_payload_forward_can(fwd_payload, &fwd_len, can_id, inner_payload, inner_len);
    return send_packet(fwd_payload, fwd_len);
}

esp_err_t vesc_set_current(uint8_t can_id, float current_a, bool is_slave)
{
    uint8_t payload[5];
    uint8_t len;
    vesc_payload_set_current(payload, &len, current_a);
    if (is_slave) return send_to_slave(can_id, payload, len);
    return send_packet(payload, len);
}

esp_err_t vesc_set_current_brake(uint8_t can_id, float brake_a, bool is_slave)
{
    uint8_t payload[5];
    uint8_t len;
    vesc_payload_set_current_brake(payload, &len, brake_a);
    if (is_slave) return send_to_slave(can_id, payload, len);
    return send_packet(payload, len);
}

esp_err_t vesc_set_handbrake(uint8_t can_id, float current_a, bool is_slave)
{
    uint8_t payload[5];
    uint8_t len;
    vesc_payload_set_handbrake(payload, &len, current_a);
    if (is_slave) return send_to_slave(can_id, payload, len);
    return send_packet(payload, len);
}

esp_err_t vesc_set_current_zero(uint8_t can_id, bool is_slave)
{
    return vesc_set_current(can_id, 0.0f, is_slave);
}

static esp_err_t read_values_response(vesc_values_t *out)
{
    uint8_t rx[BUF_SIZE];
    int rx_len = uart_read_bytes(s_port, rx, sizeof(rx),
                                 pdMS_TO_TICKS(RX_TIMEOUT_MS));
    if (rx_len < 7) {
        ESP_LOGW(TAG, "get_values: short response (%d bytes)", rx_len);
        return ESP_ERR_TIMEOUT;
    }
    if (rx[0] != 0x02) return ESP_ERR_INVALID_RESPONSE;
    uint8_t payload_len = rx[1];
    if (rx_len < payload_len + 4) return ESP_ERR_INVALID_SIZE;
    if (!vesc_response_parse_values(&rx[2], payload_len, out)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t vesc_get_values(vesc_values_t *out)
{
    uint8_t req[1];
    uint8_t req_len;
    vesc_payload_get_values(req, &req_len);

    uart_flush_input(s_port);
    esp_err_t err = send_packet(req, req_len);
    if (err != ESP_OK) return err;
    return read_values_response(out);
}

esp_err_t vesc_get_values_slave(uint8_t can_id, vesc_values_t *out)
{
    uint8_t inner[1];
    uint8_t inner_len;
    vesc_payload_get_values(inner, &inner_len);

    uint8_t fwd[64];
    uint8_t fwd_len;
    vesc_payload_forward_can(fwd, &fwd_len, can_id, inner, inner_len);

    uart_flush_input(s_port);
    esp_err_t err = send_packet(fwd, fwd_len);
    if (err != ESP_OK) return err;
    return read_values_response(out);
}
