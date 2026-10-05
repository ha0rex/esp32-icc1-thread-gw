/*
 * Standalone Matter controller + OpenThread Border Router for ESP32-S3 and
 * an external IKEA ICC-1-A OpenThread RCP.
 */

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "esp_matter.h"
#include "esp_matter_console.h"
#include "esp_matter_controller_client.h"
#include "esp_matter_controller_console.h"

#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_ot_config.h"
#include "platform/ESP32/OpenthreadLauncher.h"
#endif

#include "boot_log.h"
#include "common_macros.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_nodes.h"
#include "openthread/thread.h"
#include "homekit_bridge.h"
#include "portal.h"

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

namespace {

const char *TAG = "gateway";
bool s_otbr_initialized;

bool thread_attached(void)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return false;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole role = otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();
    return role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
           role == OT_DEVICE_ROLE_LEADER;
}

void sensor_refresh_task(void *)
{
    for (int i = 0; i < 40 && !thread_attached(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "Listening for sensor reports");
    gateway_nodes_refresh();
    for (;;) {
        // Every sensor keeps a subscription. Motion, contact, and button
        // events arrive when the device sends them. This loop only restarts
        // a subscription that has dropped.
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!thread_attached()) {
            continue;
        }
        gateway_nodes_retry_watch();
    }
}

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
#if CONFIG_OPENTHREAD_BORDER_ROUTER
        if (!s_otbr_initialized &&
            event->Platform.ESPSystemEvent.Base == IP_EVENT &&
            event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
            esp_netif_t *backbone =
                esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (!backbone) {
                ESP_LOGE(TAG, "Wi-Fi backbone interface is unavailable");
                return;
            }
            esp_openthread_set_backbone_netif(backbone);
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_openthread_lock_acquire(portMAX_DELAY);
            esp_err_t err = esp_openthread_border_router_init();
            esp_openthread_lock_release();
            if (err == ESP_OK) {
                s_otbr_initialized = true;
                ESP_LOGI(TAG, "OpenThread Border Router started");
            } else {
                ESP_LOGE(TAG, "OTBR start failed: %s", esp_err_to_name(err));
            }
        }
#endif
        break;
    default:
        break;
    }
}

esp_err_t init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    return err;
}

} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(init_nvs());
    boot_log_init();

#if CONFIG_ENABLE_CHIP_SHELL
    ESP_ERROR_CHECK(esp_matter::console::diagnostics_register_commands());
    ESP_ERROR_CHECK(esp_matter::console::wifi_register_commands());
    ESP_ERROR_CHECK(esp_matter::console::factoryreset_register_commands());
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    ESP_ERROR_CHECK(esp_matter::console::controller_register_commands());
#endif
#if CONFIG_OPENTHREAD_BORDER_ROUTER
    ESP_ERROR_CHECK(esp_matter::console::otcli_register_commands());
#endif
    ESP_ERROR_CHECK(gateway_nodes_register_console());
    ESP_ERROR_CHECK(esp_matter::console::init());
#endif

#if CONFIG_OPENTHREAD_BORDER_ROUTER
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
    ESP_LOGI(TAG, "ICC RCP UART configured: TX GPIO%d, RX GPIO%d, %d baud",
             CONFIG_ICC_UART_TX_GPIO, CONFIG_ICC_UART_RX_GPIO,
             CONFIG_ICC_UART_BAUD);
#endif

    esp_err_t err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK,
                         ESP_LOGE(TAG, "Matter start failed: %s",
                                  esp_err_to_name(err)));

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    auto &controller =
        esp_matter::controller::matter_controller_client::get_instance();
    controller.init(112233, 1, 5580);
    controller.setup_commissioner();
    esp_matter::lock::chip_stack_unlock();
#endif

    esp_err_t homekit_err = homekit_bridge_start();
    if (homekit_err != ESP_OK) {
        ESP_LOGW(TAG, "HomeKit bridge did not start: %s", esp_err_to_name(homekit_err));
    }

    esp_err_t portal_err = portal_start();
    if (portal_err != ESP_OK) {
        ESP_LOGW(TAG, "Portal did not start: %s", esp_err_to_name(portal_err));
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    xTaskCreate(sensor_refresh_task, "sensors", 8192, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Standalone Matter gateway ready");
    ESP_LOGI(TAG, "Connect Wi-Fi: matter esp wifi connect <ssid> <password>");
    ESP_LOGI(TAG, "Gateway help: matter esp gateway help");
}
