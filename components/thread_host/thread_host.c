/**
 * @file thread_host.c
 * @brief Thread network host façade over Spinel RCP.
 *
 * Phase 1: RCP health + persisted network intent in NVS.
 * Phase 2 (see docs/ARCHITECTURE.md): link ESP-IDF openthread + otbr against this UART.
 */

#include "thread_host.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "thread_host";
#define NVS_NS "thr_net"

static SemaphoreHandle_t s_mu;
static thread_host_status_t s_st;
static bool s_form_pending;
static uint8_t s_form_channel = 15;

static void load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t ch = 15;
    if (nvs_get_u8(h, "channel", &ch) == ESP_OK) {
        s_st.channel = ch;
    }
    char name[32] = {0};
    size_t nlen = sizeof(name);
    if (nvs_get_str(h, "name", name, &nlen) == ESP_OK) {
        snprintf(s_st.network_name, sizeof(s_st.network_name), "%s", name);
    }
    nvs_close(h);
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(h, "channel", s_st.channel ? s_st.channel : 15);
    if (s_st.network_name[0]) {
        (void)nvs_set_str(h, "name", s_st.network_name);
    }
    (void)nvs_commit(h);
    nvs_close(h);
}

esp_err_t thread_host_start(void)
{
    if (s_mu) {
        return ESP_OK;
    }
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_st, 0, sizeof(s_st));
    s_st.net = THREAD_NET_DOWN;
    s_st.channel = 15;
    snprintf(s_st.network_name, sizeof(s_st.network_name), "icc1-thread");
    snprintf(s_st.role_str, sizeof(s_st.role_str), "disabled");
    load_nvs();
    ESP_ERROR_CHECK(spinel_host_start());
    ESP_LOGI(TAG, "Thread host started (RCP probe active; OT stack = next integration step)");
    return ESP_OK;
}

void thread_host_get_status(thread_host_status_t *out)
{
    if (!out) {
        return;
    }
    spinel_host_status_t rcp;
    spinel_host_get_status(&rcp);
    if (!s_mu) {
        memset(out, 0, sizeof(*out));
        out->rcp = rcp;
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.rcp = rcp;
    if (rcp.link == SPINEL_LINK_OK) {
        if (s_st.net == THREAD_NET_DOWN) {
            snprintf(s_st.role_str, sizeof(s_st.role_str), "rcp-ready");
        }
    } else {
        s_st.net = THREAD_NET_DOWN;
        snprintf(s_st.role_str, sizeof(s_st.role_str), "no-rcp");
    }
    if (s_form_pending && rcp.link == SPINEL_LINK_OK) {
        /* Placeholder until otInstance is wired: record intent + channel. */
        s_st.channel = s_form_channel;
        s_st.net = THREAD_NET_ATTACHING;
        snprintf(s_st.role_str, sizeof(s_st.role_str), "form-pending");
        s_form_pending = false;
        save_nvs();
        ESP_LOGW(TAG,
                 "Form requested ch=%u — enable ESP-IDF OpenThread (docs/ARCHITECTURE.md) to "
                 "complete dataset attach",
                 (unsigned)s_st.channel);
    }
    *out = s_st;
    xSemaphoreGive(s_mu);
}

esp_err_t thread_host_request_form(uint8_t channel)
{
    if (channel < 11 || channel > 26) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mu) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_form_channel = channel;
    s_form_pending = true;
    xSemaphoreGive(s_mu);
    return ESP_OK;
}

esp_err_t thread_host_request_leave(void)
{
    if (!s_mu) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.net = THREAD_NET_DOWN;
    s_st.pan_id = 0;
    snprintf(s_st.role_str, sizeof(s_st.role_str), "left");
    xSemaphoreGive(s_mu);
    ESP_LOGI(TAG, "Leave requested (OT instance teardown when stack is linked)");
    return ESP_OK;
}
