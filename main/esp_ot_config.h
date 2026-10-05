/*
 * OpenThread host transport for the IKEA ICC-1-A EFR32MG1 RCP.
 *
 * ICC pad 2 / PB15 is RCP RX, connected to ESP32-S3 GPIO2 (host TX).
 * ICC pad 3 / PB14 is RCP TX, connected to ESP32-S3 GPIO4 (host RX).
 */
#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_openthread_types.h"
#include "sdkconfig.h"

#if CONFIG_OPENTHREAD_BORDER_ROUTER

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                      \
    {                                                                              \
        .radio_mode = RADIO_MODE_UART_RCP,                                          \
        .radio_uart_config = {                                                      \
            .port = UART_NUM_1,                                                     \
            .uart_config = {                                                        \
                .baud_rate = CONFIG_ICC_UART_BAUD,                                  \
                .data_bits = UART_DATA_8_BITS,                                      \
                .parity = UART_PARITY_DISABLE,                                      \
                .stop_bits = UART_STOP_BITS_1,                                      \
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,                              \
                .rx_flow_ctrl_thresh = 0,                                           \
                .source_clk = UART_SCLK_DEFAULT,                                    \
            },                                                                      \
            .rx_pin = (gpio_num_t)CONFIG_ICC_UART_RX_GPIO,                          \
            .tx_pin = (gpio_num_t)CONFIG_ICC_UART_TX_GPIO,                          \
        },                                                                          \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                        \
    {                                                                              \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                          \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                        \
    {                                                                              \
        .storage_partition_name = "nvs",                                            \
        .netif_queue_size = 10,                                                     \
        .task_queue_size = 10,                                                      \
    }

#endif
