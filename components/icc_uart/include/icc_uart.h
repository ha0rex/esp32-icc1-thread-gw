#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t icc_uart_init(void);
esp_err_t icc_uart_write(const uint8_t *data, size_t len);
/** Read up to max bytes; returns bytes read (0 if none within timeout_ms). */
int icc_uart_read(uint8_t *data, size_t max, uint32_t timeout_ms);
void icc_uart_flush_rx(void);

#ifdef __cplusplus
}
#endif
