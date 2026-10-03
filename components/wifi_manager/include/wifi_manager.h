#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 64
#define WIFI_SLOT_MAX 2

typedef struct {
    bool started;
    bool sta_connected;
    bool softap_on;
    char sta_ssid[WIFI_SSID_MAX + 1];
    char sta_ip[16];
    int8_t rssi;
    char softap_ssid[WIFI_SSID_MAX + 1];
} wifi_manager_status_t;

esp_err_t wifi_manager_start(void);
bool wifi_manager_is_started(void);
void wifi_manager_get_status(wifi_manager_status_t *out);

esp_err_t wifi_manager_save_sta(uint8_t slot, const char *ssid, const char *pass);
esp_err_t wifi_manager_connect_sta(uint8_t slot);
esp_err_t wifi_manager_start_softap(void);
esp_err_t wifi_manager_stop_softap(void);

/** Blocking scan; fills JSON array string into buf. */
esp_err_t wifi_manager_scan_json(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
