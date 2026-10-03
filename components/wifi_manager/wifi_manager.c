/**
 * @file wifi_manager.c
 * @brief SoftAP provisioning + STA (primary/secondary slots) for Thread gateway portal.
 */

#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "wifi_mgr";
#define NVS_NS "wifi_thr"

#define WIFI_CONNECTED_BIT BIT0

static SemaphoreHandle_t s_mu;
static EventGroupHandle_t s_eg;
static bool s_started;
static wifi_manager_status_t s_st;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    char pass[WIFI_PASS_MAX + 1];
} wifi_slot_t;

static wifi_slot_t s_slots[WIFI_SLOT_MAX];

static void load_slots(void)
{
    nvs_handle_t h;
    memset(s_slots, 0, sizeof(s_slots));
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    for (int i = 0; i < WIFI_SLOT_MAX; i++) {
        char kssid[8], kpass[8];
        snprintf(kssid, sizeof(kssid), "s%d", i);
        snprintf(kpass, sizeof(kpass), "p%d", i);
        size_t sl = sizeof(s_slots[i].ssid);
        size_t pl = sizeof(s_slots[i].pass);
        (void)nvs_get_str(h, kssid, s_slots[i].ssid, &sl);
        (void)nvs_get_str(h, kpass, s_slots[i].pass, &pl);
    }
    nvs_close(h);
}

static void save_slot(uint8_t slot)
{
    if (slot >= WIFI_SLOT_MAX) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    char kssid[8], kpass[8];
    snprintf(kssid, sizeof(kssid), "s%u", (unsigned)slot);
    snprintf(kpass, sizeof(kpass), "p%u", (unsigned)slot);
    (void)nvs_set_str(h, kssid, s_slots[slot].ssid);
    (void)nvs_set_str(h, kpass, s_slots[slot].pass);
    (void)nvs_commit(h);
    nvs_close(h);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
        s_st.sta_connected = false;
        s_st.sta_ip[0] = 0;
        xSemaphoreGive(s_mu);
        xEventGroupClearBits(s_eg, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        xSemaphoreTake(s_mu, portMAX_DELAY);
        s_st.sta_connected = true;
        snprintf(s_st.sta_ip, sizeof(s_st.sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_st.rssi = ap.rssi;
            snprintf(s_st.sta_ssid, sizeof(s_st.sta_ssid), "%s", (char *)ap.ssid);
        }
        xSemaphoreGive(s_mu);
        xEventGroupSetBits(s_eg, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "STA IP %s SSID='%s'", s_st.sta_ip, s_st.sta_ssid);
        /* Drop SoftAP once home Wi‑Fi works */
        if (s_st.softap_on) {
            (void)wifi_manager_stop_softap();
        }
    }
}

esp_err_t wifi_manager_start_softap(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "ICC1-Thread-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.softap_on = true;
    snprintf(s_st.softap_ssid, sizeof(s_st.softap_ssid), "%s", ssid);
    xSemaphoreGive(s_mu);
    ESP_LOGI(TAG, "SoftAP '%s' (open) — join and open http://192.168.4.1", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_stop_softap(void)
{
    wifi_mode_t mode = WIFI_MODE_STA;
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.softap_on = false;
    xSemaphoreGive(s_mu);
    ESP_LOGI(TAG, "SoftAP off");
    return ESP_OK;
}

esp_err_t wifi_manager_save_sta(uint8_t slot, const char *ssid, const char *pass)
{
    if (slot >= WIFI_SLOT_MAX || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    snprintf(s_slots[slot].ssid, sizeof(s_slots[slot].ssid), "%s", ssid);
    snprintf(s_slots[slot].pass, sizeof(s_slots[slot].pass), "%s", pass ? pass : "");
    xSemaphoreGive(s_mu);
    save_slot(slot);
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(uint8_t slot)
{
    if (slot >= WIFI_SLOT_MAX || !s_slots[slot].ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, s_slots[slot].ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, s_slots[slot].pass, sizeof(sta.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(s_st.softap_on ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

esp_err_t wifi_manager_scan_json(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_scan_config_t sc = {.show_hidden = false};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        snprintf(buf, buf_len, "[]");
        return ESP_FAIL;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 24) {
        n = 24;
    }
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    if (!recs) {
        snprintf(buf, buf_len, "[]");
        return ESP_ERR_NO_MEM;
    }
    uint16_t got = n;
    esp_wifi_scan_get_ap_records(&got, recs);
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, buf_len - pos, "[");
    for (uint16_t i = 0; i < got && pos + 80 < buf_len; i++) {
        pos += (size_t)snprintf(buf + pos, buf_len - pos,
                                "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u}", i ? "," : "",
                                (char *)recs[i].ssid, (int)recs[i].rssi, (unsigned)recs[i].primary);
    }
    snprintf(buf + pos, buf_len - pos, "]");
    free(recs);
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_mu = xSemaphoreCreateMutex();
    s_eg = xEventGroupCreate();
    if (!s_mu || !s_eg) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_st, 0, sizeof(s_st));
    load_slots();

    ESP_ERROR_CHECK(esp_netif_init());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    (void)s_sta_netif;
    (void)s_ap_netif;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_started = true;
    s_st.started = true;

    if (s_slots[0].ssid[0]) {
        ESP_LOGI(TAG, "Connecting to home Wi-Fi '%s' (SoftAP if STA fails)", s_slots[0].ssid);
        (void)wifi_manager_start_softap();
        (void)wifi_manager_connect_sta(0);
        /* If no IP in 12s, keep SoftAP for provisioning */
        EventBits_t bits =
            xEventGroupWaitBits(s_eg, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(12000));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "STA not up — SoftAP remains for setup");
        }
    } else {
        ESP_LOGI(TAG, "No STA credentials — SoftAP provisioning");
        ESP_ERROR_CHECK(wifi_manager_start_softap());
    }
    return ESP_OK;
}

bool wifi_manager_is_started(void)
{
    return s_started;
}

void wifi_manager_get_status(wifi_manager_status_t *out)
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
    if (s_st.sta_connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
        }
    }
    xSemaphoreGive(s_mu);
}
