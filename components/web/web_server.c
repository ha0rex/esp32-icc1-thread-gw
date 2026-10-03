/**
 * @file web_server.c
 * @brief HTTP portal + JSON APIs for Thread / Matter gateway.
 */

#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "matter_bridge.h"
#include "sdkconfig.h"
#include "thread_host.h"
#include "web_app.h"
#include "wifi_manager.h"

static const char *TAG = "web";
static httpd_handle_t s_srv;
static char s_json[3072];

static const char *rcp_link_str(spinel_link_status_t s)
{
    switch (s) {
    case SPINEL_LINK_OK:
        return "ok";
    case SPINEL_LINK_NO_RESPONSE:
        return "no_response";
    case SPINEL_LINK_NO_UART:
        return "no_uart";
    default:
        return "unknown";
    }
}

static void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    if (!in) {
        in = "";
    }
    while (*in && o + 2 < out_len) {
        char c = *in++;
        if (c == '"' || c == '\\') {
            if (o + 3 >= out_len) {
                break;
            }
            out[o++] = '\\';
            out[o++] = c;
        } else if ((unsigned char)c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
}

static esp_err_t send_json_buf(httpd_req_t *req, const char *s)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, WEB_APP_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    thread_host_status_t th;
    wifi_manager_status_t wifi;
    matter_bridge_status_t matter;
    thread_host_get_status(&th);
    wifi_manager_get_status(&wifi);
    matter_bridge_get_status(&matter);

    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    char ncp_e[120], ssid_e[40], ip_e[20], ap_e[40], role_e[24], name_e[40];
    char pin_e[16], disc_e[12], prod_e[48];
    json_escape(th.rcp.ncp_version, ncp_e, sizeof(ncp_e));
    json_escape(wifi.sta_ssid, ssid_e, sizeof(ssid_e));
    json_escape(wifi.sta_ip, ip_e, sizeof(ip_e));
    json_escape(wifi.softap_ssid, ap_e, sizeof(ap_e));
    json_escape(th.role_str, role_e, sizeof(role_e));
    json_escape(th.network_name, name_e, sizeof(name_e));
    json_escape(matter.setup_pin, pin_e, sizeof(pin_e));
    json_escape(matter.discriminator, disc_e, sizeof(disc_e));
    json_escape(matter.product_name, prod_e, sizeof(prod_e));

    int64_t up_s = esp_timer_get_time() / 1000000LL;
    snprintf(s_json, sizeof(s_json),
             "{\"board\":{\"chip\":\"%s\",\"uart_tx\":%d,\"uart_rx\":%d,\"uart_baud\":%d,\"cores\":%d},"
             "\"rcp\":{\"link\":\"%s\",\"ncp_version\":\"%s\",\"rx_bytes\":%u,\"tx_bytes\":%u,"
             "\"probes_ok\":%u,\"probes_fail\":%u},"
             "\"thread\":{\"role\":\"%s\",\"channel\":%u,\"pan_id\":%u,\"network_name\":\"%s\","
             "\"device_count\":%u},"
             "\"wifi\":{\"sta_connected\":%s,\"softap_on\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
             "\"softap_ssid\":\"%s\",\"rssi\":%d},"
             "\"matter\":{\"started\":%s,\"fabric_commissioned\":%s,\"endpoint_count\":%u,"
             "\"setup_pin\":\"%s\",\"discriminator\":\"%s\",\"product_name\":\"%s\"},"
             "\"fw_version\":\"%s\",\"uptime\":\"%lldh %lldm %llds\",\"free_heap\":%u,"
             "\"min_free_heap\":%u}",
             CONFIG_IDF_TARGET, CONFIG_ICC_UART_TX_GPIO, CONFIG_ICC_UART_RX_GPIO, CONFIG_ICC_UART_BAUD,
             (int)chip.cores, rcp_link_str(th.rcp.link), ncp_e, (unsigned)th.rcp.rx_bytes,
             (unsigned)th.rcp.tx_bytes, (unsigned)th.rcp.probes_ok, (unsigned)th.rcp.probes_fail,
             role_e, (unsigned)th.channel, (unsigned)th.pan_id, name_e, (unsigned)th.device_count,
             wifi.sta_connected ? "true" : "false", wifi.softap_on ? "true" : "false", ssid_e, ip_e,
             ap_e, (int)wifi.rssi, matter.started ? "true" : "false",
             matter.fabric_commissioned ? "true" : "false", (unsigned)matter.endpoint_count, pin_e,
             disc_e, prod_e, app ? app->version : "?", (long long)(up_s / 3600),
             (long long)((up_s % 3600) / 60), (long long)(up_s % 60),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    return send_json_buf(req, s_json);
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buflen)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= buflen) {
        return ESP_ERR_INVALID_SIZE;
    }
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, buf + got, total - got);
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += n;
    }
    buf[got] = 0;
    return ESP_OK;
}

static int json_get_int(const char *body, const char *key, int def)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) {
        return def;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return def;
    }
    return atoi(p + 1);
}

static bool json_get_str(const char *body, const char *key, char *out, size_t out_len)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) {
        return false;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return false;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_len) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    return true;
}

static esp_err_t thread_form_post(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    int ch = json_get_int(body, "channel", 15);
    esp_err_t e = thread_host_request_form((uint8_t)ch);
    snprintf(s_json, sizeof(s_json), "{\"status\":\"%s\",\"channel\":%d}",
             e == ESP_OK ? "ok" : "error", ch);
    return send_json_buf(req, s_json);
}

static esp_err_t thread_leave_post(httpd_req_t *req)
{
    (void)req;
    (void)thread_host_request_leave();
    return send_json_buf(req, "{\"status\":\"ok\"}");
}

static esp_err_t wifi_sta_post(httpd_req_t *req)
{
    char body[192];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    char ssid[33] = {0};
    char pass[65] = {0};
    int slot = json_get_int(body, "slot", 0);
    if (!json_get_str(body, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid");
        return ESP_FAIL;
    }
    (void)json_get_str(body, "password", pass, sizeof(pass));
    esp_err_t e = wifi_manager_save_sta((uint8_t)slot, ssid, pass);
    if (e == ESP_OK) {
        e = wifi_manager_connect_sta((uint8_t)slot);
    }
    snprintf(s_json, sizeof(s_json), "{\"status\":\"%s\"}", e == ESP_OK ? "ok" : "error");
    return send_json_buf(req, s_json);
}

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    char *buf = malloc(4096);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    esp_err_t e = wifi_manager_scan_json(buf, 4096);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, e == ESP_OK ? buf : "[]");
    free(buf);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_srv) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    ESP_ERROR_CHECK(httpd_start(&s_srv, &cfg));

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/thread/form", .method = HTTP_POST, .handler = thread_form_post},
        {.uri = "/api/thread/leave", .method = HTTP_POST, .handler = thread_leave_post},
        {.uri = "/api/wifi/sta", .method = HTTP_POST, .handler = wifi_sta_post},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_srv, &uris[i]));
    }
    ESP_LOGI(TAG, "Production portal on port 80");
    return ESP_OK;
}
