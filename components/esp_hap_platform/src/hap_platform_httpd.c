/*
 * Local override of the Zigbee gateway's hap_platform_httpd.c.
 *
 * Pair-setup saves the controller key with NVS on this task. NVS disables the
 * flash cache, and spi_flash_disable_interrupts_caches_and_other_cpu asserts
 * that the stack is in internal DRAM. A PSRAM stack fails that assert
 * (illegal instruction at 0) and the chip reboots in the middle of Add to Home.
 *
 * The stack is therefore internal. It is reserved before listen(): if the
 * task then fails to start, esp_http_server leaves the port bound.
 */
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hap_httpd";
static const size_t kStackBytes = 16384;

static void *s_stack_reserve;

int hap_platform_httpd_reserve_stack(void)
{
    if (s_stack_reserve) {
        return 0;
    }
    s_stack_reserve = heap_caps_malloc(kStackBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_in = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (!s_stack_reserve) {
        ESP_LOGE(TAG, "internal httpd stack unavailable (%u bytes, free=%u largest=%u)",
                 (unsigned)kStackBytes, (unsigned)free_in, (unsigned)largest);
        return -1;
    }
    ESP_LOGI(TAG, "holding %u-byte internal httpd stack (free=%u largest=%u)",
             (unsigned)kStackBytes, (unsigned)free_in, (unsigned)largest);
    return 0;
}

void hap_platform_httpd_release_stack(void)
{
    if (s_stack_reserve) {
        heap_caps_free(s_stack_reserve);
        s_stack_reserve = NULL;
    }
}

httpd_handle_t *int_handle;
int hap_platform_httpd_start(httpd_handle_t *handle)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 3;
    /* 8 KB overflowed on encrypted /characteristics reads. 16 KB fits those
     * handlers and pair-setup, and it has to live in internal RAM. */
    config.stack_size = kStackBytes;
    config.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    config.server_port = CONFIG_HAP_HTTP_SERVER_PORT;
    config.ctrl_port = CONFIG_HAP_HTTP_CONTROL_PORT;
    /* Home keeps one idle socket open for EVENT/1.0. LRU eviction deletes that
     * socket first, and without keepalive the AP drops it after 30–60s with no
     * RST. iOS then shows stale tiles until its fallback poll or a relaunch.
     * Leave the event socket in place and probe it before the AP idle timer. */
    config.max_open_sockets = 8;
    config.max_uri_handlers = CONFIG_HAP_HTTP_MAX_URI_HANDLERS;
    config.max_resp_headers = 8;
    config.backlog_conn = 4;
    config.lru_purge_enable = false;
    config.recv_wait_timeout = 8;
    config.send_wait_timeout = 8;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 15;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;

    if (s_stack_reserve) {
        heap_caps_free(s_stack_reserve);
        s_stack_reserve = NULL;
    } else if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kStackBytes) {
        ESP_LOGE(TAG, "refusing httpd_start: largest internal block %u < %u",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)kStackBytes);
        return ESP_ERR_NO_MEM;
    }
    size_t free_in = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "httpd_start (port %d socks %d stack %d internal free=%u largest=%u)",
             config.server_port, config.max_open_sockets, config.stack_size,
             (unsigned)free_in, (unsigned)largest);
    esp_err_t err = httpd_start(handle, &config);
    if (err == ESP_OK) {
        int_handle = handle;
        return err;
    }
    ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return err;
}

int hap_platform_httpd_stop(httpd_handle_t *handle)
{
    esp_err_t err = httpd_stop(*handle);
    if (err == ESP_OK) {
        int_handle = NULL;
    }
    return err;
}

int hap_platform_httpd_get_port()
{
    return CONFIG_HAP_HTTP_SERVER_PORT;
}

void *hap_platform_httpd_get_sess_ctx(httpd_req_t *req)
{
    if (req) {
        return req->sess_ctx;
    }
    return NULL;
}

esp_err_t hap_platform_httpd_set_sess_ctx(httpd_req_t *req, void *ctx, httpd_free_ctx_fn_t free_ctx, bool ignore_ctx_changes)
{
    if (req) {
        req->sess_ctx = ctx;
        req->free_ctx = free_ctx;
#ifndef CONFIG_IDF_TARGET_ESP8266
        req->ignore_sess_ctx_changes = ignore_ctx_changes;
#endif
        return ESP_OK;
    }
    return ESP_FAIL;
}

static char *hap_platform_httpd_rqtype_to_string(int method)
{
    switch (method) {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    default:
        return "INVALID";
    }
}

const char *hap_platform_httpd_get_req_method(httpd_req_t *req)
{
    if (req) {
        return hap_platform_httpd_rqtype_to_string(req->method);
    }
    return NULL;
}

const char *hap_platform_httpd_get_req_uri(httpd_req_t *req)
{
    if (req) {
        return req->uri;
    }
    return NULL;
}

int hap_platform_httpd_get_content_len(httpd_req_t *req)
{
    if (req) {
        return req->content_len;
    }
    return -1;
}

httpd_handle_t *hap_platform_httpd_get_handle()
{
    return int_handle;
}
