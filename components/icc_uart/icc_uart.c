/**
 * @file icc_uart.c
 * @brief UART link to ICC-1-A (OpenThread RCP Spinel/HDLC).
 */

#include "icc_uart.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "icc_uart";

#define ICC_UART_NUM UART_NUM_1
#define ICC_UART_BUF 2048

esp_err_t icc_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = CONFIG_ICC_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(ICC_UART_NUM, ICC_UART_BUF, ICC_UART_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ICC_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(ICC_UART_NUM, CONFIG_ICC_UART_TX_GPIO, CONFIG_ICC_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "Init UART%d TX=GPIO%d RX=GPIO%d @ %d 8N1 (no flow control)", (int)ICC_UART_NUM,
             CONFIG_ICC_UART_TX_GPIO, CONFIG_ICC_UART_RX_GPIO, CONFIG_ICC_UART_BAUD);
    ESP_LOGI(TAG, "UART ready");
    return ESP_OK;
}

esp_err_t icc_uart_write(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = uart_write_bytes(ICC_UART_NUM, data, len);
    return (n == (int)len) ? ESP_OK : ESP_FAIL;
}

int icc_uart_read(uint8_t *data, size_t max, uint32_t timeout_ms)
{
    if (!data || max == 0) {
        return 0;
    }
    return uart_read_bytes(ICC_UART_NUM, data, max, pdMS_TO_TICKS(timeout_ms));
}

void icc_uart_flush_rx(void)
{
    uart_flush_input(ICC_UART_NUM);
}
