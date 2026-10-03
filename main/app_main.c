/**
 * @file app_main.c
 * @brief ESP32 host for IKEA ICC-1-A OpenThread RCP (Spinel/HDLC over UART)
 *        with Matter bridge scaffolding and a SoftAP/STA web portal.
 *
 * Independent of esp32-icc1-zigbee-gw (Zigbee + HomeKit). Same UX goals;
 * Thread/Matter stack instead of EZSP/HomeKit.
 */

#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "matter_bridge.h"
#include "thread_host.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";

#if CONFIG_MATTER_BRIDGE_ENABLED
static void delayed_matter_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(8000));
    esp_err_t e = matter_bridge_start();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "Matter bridge start: %s", esp_err_to_name(e));
    }
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    esp_reset_reason_t rr = esp_reset_reason();
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "%s + ICC-1-A Thread RCP gateway", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "UART TX=GPIO%d RX=GPIO%d @ %d (Spinel)", CONFIG_ICC_UART_TX_GPIO,
             CONFIG_ICC_UART_RX_GPIO, CONFIG_ICC_UART_BAUD);
    ESP_LOGI(TAG, "Reset reason: %d", (int)rr);
    ESP_LOGI(TAG, "================================================");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_WIFI_ENABLED
    ESP_ERROR_CHECK(wifi_manager_start());
    if (wifi_manager_is_started()) {
        ESP_ERROR_CHECK(web_server_start());
    }
#else
    ESP_LOGW(TAG, "Wi-Fi disabled; Thread RCP probing continues");
#endif

#if CONFIG_THREAD_HOST_ENABLED
    ESP_ERROR_CHECK(thread_host_start());
#endif

#if CONFIG_MATTER_BRIDGE_ENABLED
    if (xTaskCreate(delayed_matter_task, "matter_delay", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Matter delay task failed — starting immediately");
        (void)matter_bridge_start();
    } else {
        ESP_LOGI(TAG, "Matter start deferred 8s (Wi‑Fi settle)");
    }
#endif
}
