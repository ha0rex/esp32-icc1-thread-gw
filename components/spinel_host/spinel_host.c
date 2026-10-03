/**
 * @file spinel_host.c
 * @brief Minimal Spinel/HDLC probe for OpenThread RCP on ICC-1-A.
 *
 * Confirms the radio answers Spinel before full OTBR/Matter is wired.
 * Not a complete OpenThread stack — see thread_host + docs/MATTER.md.
 */

#include "spinel_host.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "icc_uart.h"

static const char *TAG = "spinel";

#define HDLC_FLAG 0x7E
#define HDLC_ESC  0x7D
#define HDLC_XOR  0x20

/* Spinel header: IID=0, TID=1, CMD=PROP_VALUE_GET(2), PROP_NCP_VERSION(1) */
#define SPINEL_CMD_PROP_VALUE_GET 2
#define SPINEL_PROP_NCP_VERSION   1

static SemaphoreHandle_t s_mu;
static spinel_host_status_t s_st;
static TaskHandle_t s_task;

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static size_t hdlc_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    if (out_max < 4) {
        return 0;
    }
    size_t o = 0;
    out[o++] = HDLC_FLAG;
    uint16_t crc = crc16_ccitt(in, in_len);
    uint8_t tmp[256];
    if (in_len + 2 > sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, in, in_len);
    tmp[in_len] = (uint8_t)(crc & 0xFF);
    tmp[in_len + 1] = (uint8_t)(crc >> 8);
    for (size_t i = 0; i < in_len + 2; i++) {
        uint8_t b = tmp[i];
        if (b == HDLC_FLAG || b == HDLC_ESC) {
            if (o + 2 >= out_max) {
                return 0;
            }
            out[o++] = HDLC_ESC;
            out[o++] = (uint8_t)(b ^ HDLC_XOR);
        } else {
            if (o + 1 >= out_max) {
                return 0;
            }
            out[o++] = b;
        }
    }
    if (o + 1 >= out_max) {
        return 0;
    }
    out[o++] = HDLC_FLAG;
    return o;
}

static bool hdlc_try_extract(const uint8_t *buf, size_t len, uint8_t *payload, size_t *payload_len)
{
    size_t start = SIZE_MAX;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == HDLC_FLAG) {
            if (start == SIZE_MAX) {
                start = i + 1;
            } else if (i > start) {
                uint8_t tmp[256];
                size_t t = 0;
                for (size_t j = start; j < i && t < sizeof(tmp); j++) {
                    if (buf[j] == HDLC_ESC && j + 1 < i) {
                        tmp[t++] = (uint8_t)(buf[j + 1] ^ HDLC_XOR);
                        j++;
                    } else {
                        tmp[t++] = buf[j];
                    }
                }
                if (t < 3) {
                    start = i + 1;
                    continue;
                }
                uint16_t got = (uint16_t)tmp[t - 2] | ((uint16_t)tmp[t - 1] << 8);
                uint16_t expect = crc16_ccitt(tmp, t - 2);
                if (got != expect) {
                    start = i + 1;
                    continue;
                }
                size_t plen = t - 2;
                if (plen > *payload_len) {
                    plen = *payload_len;
                }
                memcpy(payload, tmp, plen);
                *payload_len = plen;
                return true;
            }
        }
    }
    return false;
}

esp_err_t spinel_host_probe_once(void)
{
    /* Frame: HDR(0x80|tid1)=0x81, CMD=2, PROP=1 (packed uint) */
    uint8_t spinel[] = {0x81, SPINEL_CMD_PROP_VALUE_GET, SPINEL_PROP_NCP_VERSION};
    uint8_t framed[64];
    size_t flen = hdlc_encode(spinel, sizeof(spinel), framed, sizeof(framed));
    if (flen == 0) {
        return ESP_ERR_NO_MEM;
    }
    icc_uart_flush_rx();
    if (icc_uart_write(framed, flen) != ESP_OK) {
        return ESP_FAIL;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.tx_bytes += (uint32_t)flen;
    xSemaphoreGive(s_mu);

    uint8_t rx[512];
    size_t got = 0;
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) < 500000) { /* 500 ms */
        int n = icc_uart_read(rx + got, sizeof(rx) - got, 50);
        if (n > 0) {
            got += (size_t)n;
            xSemaphoreTake(s_mu, portMAX_DELAY);
            s_st.rx_bytes += (uint32_t)n;
            xSemaphoreGive(s_mu);
            uint8_t payload[256];
            size_t plen = sizeof(payload);
            if (hdlc_try_extract(rx, got, payload, &plen) && plen >= 3) {
                /* Skip hdr+cmd+prop; remainder is UTF-8 version string for VALUE_IS */
                size_t off = 3;
                while (off < plen && payload[off] < 0x20) {
                    off++;
                }
                size_t vlen = plen - off;
                if (vlen >= sizeof(s_st.ncp_version)) {
                    vlen = sizeof(s_st.ncp_version) - 1;
                }
                xSemaphoreTake(s_mu, portMAX_DELAY);
                memcpy(s_st.ncp_version, payload + off, vlen);
                s_st.ncp_version[vlen] = 0;
                /* Strip trailing junk */
                for (size_t i = 0; i < vlen; i++) {
                    if (s_st.ncp_version[i] < 0x20 || s_st.ncp_version[i] > 0x7e) {
                        s_st.ncp_version[i] = 0;
                        break;
                    }
                }
                s_st.link = SPINEL_LINK_OK;
                s_st.probes_ok++;
                xSemaphoreGive(s_mu);
                ESP_LOGI(TAG, "RCP OK version='%s'", s_st.ncp_version);
                return ESP_OK;
            }
        }
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.link = SPINEL_LINK_NO_RESPONSE;
    s_st.probes_fail++;
    xSemaphoreGive(s_mu);
    ESP_LOGW(TAG, "No Spinel response (flash OT-RCP @ %d baud? TX/RX crossed?)",
             CONFIG_ICC_UART_BAUD);
    return ESP_ERR_TIMEOUT;
}

static void spinel_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    for (;;) {
        (void)spinel_host_probe_once();
        vTaskDelay(pdMS_TO_TICKS(s_st.link == SPINEL_LINK_OK ? 15000 : 5000));
    }
}

esp_err_t spinel_host_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_st, 0, sizeof(s_st));
    s_st.link = SPINEL_LINK_UNKNOWN;
    ESP_ERROR_CHECK(icc_uart_init());
    if (xTaskCreate(spinel_task, "spinel", 4096, NULL, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Spinel probe task running");
    return ESP_OK;
}

void spinel_host_get_status(spinel_host_status_t *out)
{
    if (!out) {
        return;
    }
    if (!s_mu) {
        memset(out, 0, sizeof(*out));
        out->link = SPINEL_LINK_NO_UART;
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_mu);
}
