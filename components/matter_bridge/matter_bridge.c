/**
 * @file matter_bridge.c
 * @brief Matter bridge status + commissioning placeholders.
 *
 * Full CHIP / ESP-Matter integration is documented in docs/MATTER.md.
 * This module always builds so the portal and UX match the Zigbee/HomeKit twin
 * before the heavyweight Matter SDK is linked.
 */

#include "matter_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "matter";

static SemaphoreHandle_t s_mu;
static matter_bridge_status_t s_st;

esp_err_t matter_bridge_start(void)
{
    if (s_mu) {
        return ESP_OK;
    }
    s_mu = xSemaphoreCreateMutex();
    if (!s_mu) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_st, 0, sizeof(s_st));
    s_st.started = true;
    s_st.fabric_commissioned = false;
    s_st.endpoint_count = 1; /* root aggregator placeholder */
    snprintf(s_st.discriminator, sizeof(s_st.discriminator), "3840");
    snprintf(s_st.setup_pin, sizeof(s_st.setup_pin), "20202021");
    snprintf(s_st.vendor_name, sizeof(s_st.vendor_name), "ICC");
    snprintf(s_st.product_name, sizeof(s_st.product_name), "ICC Thread Gateway");
    ESP_LOGI(TAG, "Matter scaffold ready — PIN %s discriminator %s", s_st.setup_pin,
             s_st.discriminator);
    ESP_LOGI(TAG, "Link ESP-Matter (docs/MATTER.md) for real commissioning / bridging");
    return ESP_OK;
}

void matter_bridge_get_status(matter_bridge_status_t *out)
{
    if (!out) {
        return;
    }
    if (!s_mu) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_mu);
}
