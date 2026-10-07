#include "portal.h"
#include "peer_link.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_log.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gateway_nodes.h"
#include "homekit_bridge.h"
#include "matter_sniff.h"
#include "openthread/dataset.h"
#include "openthread/link.h"
#include "openthread/thread.h"
#include "portal_html.h"
#include "sdkconfig.h"

#include "esp_matter.h"
#include "esp_matter_controller_pairing_command.h"

namespace {

const char *TAG = "portal";
httpd_handle_t s_server;
SemaphoreHandle_t s_lock;
char s_pair_msg[160] = "Idle";
bool s_pairing;
uint64_t s_pair_node;
uint16_t s_pair_endpoint = 1;
char s_pair_kind[16] = "contact";
char s_pair_name[24];
uint8_t s_dataset[OT_OPERATIONAL_DATASET_MAX_LENGTH];
uint8_t s_dataset_len;

void set_pair_msg(const char *msg)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    snprintf(s_pair_msg, sizeof(s_pair_msg), "%s", msg ? msg : "");
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void copy_pair_msg(char *out, size_t out_len)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    snprintf(out, out_len, "%s", s_pair_msg);
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    if (!in) {
        in = "";
    }
    while (*in && o + 2 < out_len) {
        unsigned char c = static_cast<unsigned char>(*in++);
        if (c == '"' || c == '\\') {
            if (o + 3 >= out_len) {
                break;
            }
            out[o++] = '\\';
            out[o++] = static_cast<char>(c);
        } else if (c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = static_cast<char>(c);
        }
    }
    out[o] = 0;
}

void json_escape_multiline(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    if (!in) {
        in = "";
    }
    while (*in && o + 2 < out_len) {
        unsigned char c = static_cast<unsigned char>(*in++);
        if (c == '"' || c == '\\' || c == '\n') {
            if (o + 3 >= out_len) {
                break;
            }
            out[o++] = '\\';
            out[o++] = c == '\n' ? 'n' : static_cast<char>(c);
        } else if (c == '\r') {
            continue;
        } else if (c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = static_cast<char>(c);
        }
    }
    out[o] = 0;
}

bool json_find_string(const char *body, const char *key, char *out, size_t out_len)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(body, pattern);
    if (!p) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '"') {
        return false;
    }
    ++p;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_len) {
        if (*p == '\\' && p[1]) {
            ++p;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    return true;
}

bool json_find_u64(const char *body, const char *key, uint64_t &value)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(body, pattern);
    if (!p) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return false;
    }
    value = strtoull(p + 1, nullptr, 10);
    return true;
}

void on_pase(CHIP_ERROR err)
{
    if (err == CHIP_NO_ERROR) {
        set_pair_msg("Sensor found, commissioning…");
    } else {
        s_pairing = false;
        set_pair_msg(err == CHIP_ERROR_TIMEOUT
                         ? "The sensor did not answer. Press its system button, then pair again."
                         : "Could not reach the device. Factory-reset it and try again.");
    }
}

void on_commission_ok(chip::ScopedNodeId peer)
{
    (void)peer;
    s_pairing = false;
    esp_err_t err = gateway_nodes_remember(s_pair_node, s_pair_endpoint,
                                           s_pair_kind, s_pair_name);
    set_pair_msg(err == ESP_OK ? "Paired and subscribed"
                               : "Paired, but saving the device failed");
}

void on_commission_fail(
    chip::ScopedNodeId peer, CHIP_ERROR error,
    chip::Controller::CommissioningStage stage,
    std::optional<chip::Credentials::AttestationVerificationResult> extra)
{
    (void)peer;
    (void)stage;
    (void)extra;
    s_pairing = false;
    char msg[160];
    snprintf(msg, sizeof(msg), "Pairing failed: %s", chip::ErrorStr(error));
    set_pair_msg(msg);
}

bool read_thread(char *role, size_t role_len, char *name, size_t name_len,
                 char *pan, size_t pan_len, char *ext_pan, size_t ext_len,
                 char *partition, size_t part_len, unsigned &channel,
                 char *dataset_hex, size_t dataset_hex_len, bool &rcp_ok)
{
    snprintf(role, role_len, "down");
    name[0] = pan[0] = ext_pan[0] = partition[0] = dataset_hex[0] = 0;
    channel = 0;
    rcp_ok = false;

    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return false;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otDeviceRole device_role = otThreadGetDeviceRole(instance);
    snprintf(role, role_len, "%s", otThreadDeviceRoleToString(device_role));
    rcp_ok = device_role != OT_DEVICE_ROLE_DISABLED;
    const char *network = otThreadGetNetworkName(instance);
    snprintf(name, name_len, "%s", network ? network : "");
    channel = otLinkGetChannel(instance);
    snprintf(pan, pan_len, "0x%04x", otLinkGetPanId(instance));
    const otExtendedPanId *ext = otThreadGetExtendedPanId(instance);
    if (ext) {
        snprintf(ext_pan, ext_len, "%02x%02x%02x%02x%02x%02x%02x%02x",
                 ext->m8[0], ext->m8[1], ext->m8[2], ext->m8[3], ext->m8[4],
                 ext->m8[5], ext->m8[6], ext->m8[7]);
    }
    snprintf(partition, part_len, "0x%lx",
             static_cast<unsigned long>(otThreadGetPartitionId(instance)));
    otOperationalDatasetTlvs tlvs;
    if (otDatasetGetActiveTlvs(instance, &tlvs) == OT_ERROR_NONE &&
        tlvs.mLength > 0 && tlvs.mLength <= sizeof(s_dataset)) {
        memcpy(s_dataset, tlvs.mTlvs, tlvs.mLength);
        s_dataset_len = tlvs.mLength;
        size_t used = 0;
        for (uint8_t i = 0; i < tlvs.mLength && used + 3 < dataset_hex_len; ++i) {
            used += static_cast<size_t>(
                snprintf(dataset_hex + used, dataset_hex_len - used, "%02x",
                         tlvs.mTlvs[i]));
        }
    }
    esp_openthread_lock_release();
    return true;
}

esp_err_t read_body(httpd_req_t *req, char *buf, size_t buflen)
{
    if (req->content_len <= 0 || req->content_len >= buflen) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += static_cast<size_t>(n);
    }
    buf[got] = 0;
    return ESP_OK;
}

esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_error(httpd_req_t *req, const char *msg)
{
    char body[180];
    char escaped[120];
    json_escape(msg, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", escaped);
    httpd_resp_set_status(req, "400 Bad Request");
    return send_json(req, body);
}

} // namespace

extern "C" esp_err_t portal_root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

extern "C" esp_err_t portal_status_get(httpd_req_t *req)
{
    EXT_RAM_BSS_ATTR static char json[16384];
    static char nodes[5120];
    EXT_RAM_BSS_ATTR static char panic_e[1400];
    EXT_RAM_BSS_ATTR static char log_e[2200];
    char role[24], name[40], pan[16], ext_pan[24], partition[24];
    char dataset[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
    char name_e[80], role_e[48], pair_e[200], ssid_e[80], ip[20];
    char pair_msg[160];
    unsigned channel = 0;
    bool rcp_ok = false;
    read_thread(role, sizeof(role), name, sizeof(name), pan, sizeof(pan),
                ext_pan, sizeof(ext_pan), partition, sizeof(partition), channel,
                dataset, sizeof(dataset), rcp_ok);
    if (gateway_nodes_write_json(nodes, sizeof(nodes)) != ESP_OK) {
        snprintf(nodes, sizeof(nodes), "[]");
    }
    copy_pair_msg(pair_msg, sizeof(pair_msg));

    bool sta = false;
    int rssi = 0;
    char ssid[33] = "";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        sta = true;
        rssi = ap.rssi;
        memcpy(ssid, ap.ssid, sizeof(ssid) - 1);
    }
    ip[0] = 0;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr) {
        esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
    }

    json_escape(name, name_e, sizeof(name_e));
    json_escape(role, role_e, sizeof(role_e));
    json_escape(pair_msg, pair_e, sizeof(pair_e));
    json_escape(ssid, ssid_e, sizeof(ssid_e));

    int64_t up = esp_timer_get_time() / 1000000LL;
    const esp_app_desc_t *app = esp_app_get_description();
    homekit_bridge_status_t homekit = {};
    homekit_bridge_get_status(&homekit);
    char hk_code[32];
    char hk_status[80];
    char hk_uri[64];
    json_escape(homekit.setup_code, hk_code, sizeof(hk_code));
    json_escape(homekit.status, hk_status, sizeof(hk_status));
    json_escape(homekit.setup_uri, hk_uri, sizeof(hk_uri));
    json_escape_multiline(boot_log_panic(), panic_e, sizeof(panic_e));
    json_escape_multiline(boot_log_previous(), log_e, sizeof(log_e));
    snprintf(json, sizeof(json),
             "{\"board\":{\"chip\":\"%s\",\"uart_tx\":%d,\"uart_rx\":%d,\"uart_baud\":%d},"
             "\"thread\":{\"role\":\"%s\",\"rcp\":\"%s\",\"network_name\":\"%s\","
             "\"channel\":%u,\"pan_id\":\"%s\",\"ext_pan\":\"%s\",\"partition\":\"%s\","
             "\"dataset\":\"%s\"},"
             "\"wifi\":{\"sta_connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
             "\"homekit\":{\"started\":%s,\"paired\":%s,\"setup_code\":\"%s\","
             "\"setup_id\":\"%s\",\"setup_uri\":\"%s\",\"accessories\":%u,\"status\":\"%s\"},"
             "\"nodes\":%s,\"pairing\":\"%s\",\"fw_version\":\"%s\","
             "\"uptime\":\"%lldh %lldm\",\"free_heap\":%u,"
             "\"heap_internal\":%u,\"heap_psram\":%u,"
             "\"boot\":{\"reset\":\"%s\",\"panic\":\"%s\",\"log\":\"%s\"}}",
             CONFIG_IDF_TARGET, CONFIG_ICC_UART_TX_GPIO, CONFIG_ICC_UART_RX_GPIO,
             CONFIG_ICC_UART_BAUD, role_e, rcp_ok ? "ok" : "down", name_e, channel,
             pan, ext_pan, partition, dataset, sta ? "true" : "false", ssid_e, ip,
             rssi, homekit.started ? "true" : "false", homekit.paired ? "true" : "false",
             hk_code, homekit.setup_id, hk_uri, static_cast<unsigned>(homekit.accessory_count),
             hk_status, nodes, pair_e, app ? app->version : "",
             up / 3600, (up % 3600) / 60,
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             boot_log_reset_name(), panic_e, log_e);
    return send_json(req, json);
}

extern "C" esp_err_t portal_pair_post(httpd_req_t *req)
{
    char body[512];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Request is too large");
    }
    if (s_pairing) {
        return send_error(req, "Pairing is already in progress");
    }
    char payload[180];
    char kind[16];
    char name[24];
    if (!json_find_string(body, "payload", payload, sizeof(payload)) || !payload[0]) {
        return send_error(req, "Matter setup code is required");
    }
    // The manual code is printed as 1391-183-1983. CHIP wants the digits only.
    char compact[180];
    size_t compact_len = 0;
    for (const char *p = payload; *p && compact_len + 1 < sizeof(compact); ++p) {
        if (*p == '-' || *p == ' ') {
            continue;
        }
        compact[compact_len++] = *p;
    }
    compact[compact_len] = 0;
    if (!compact[0]) {
        return send_error(req, "Matter setup code is required");
    }
    snprintf(payload, sizeof(payload), "%s", compact);
    if (!json_find_string(body, "kind", kind, sizeof(kind))) {
        snprintf(kind, sizeof(kind), "contact");
    }
    if (!json_find_string(body, "name", name, sizeof(name))) {
        name[0] = 0;
    }
    uint64_t endpoint = 1;
    json_find_u64(body, "endpoint", endpoint);
    if (endpoint == 0 || endpoint > UINT16_MAX) {
        return send_error(req, "Endpoint is invalid");
    }
    if (s_dataset_len == 0) {
        return send_error(req, "Thread network is not up yet");
    }

    s_pair_node = gateway_nodes_next_id();
    s_pair_endpoint = static_cast<uint16_t>(endpoint);
    snprintf(s_pair_kind, sizeof(s_pair_kind), "%s", kind);
    snprintf(s_pair_name, sizeof(s_pair_name), "%s", name[0] ? name : kind);
    s_pairing = true;
    set_pair_msg("Looking for the device…");

    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .pase_callback = on_pase,
        .commissioning_success_callback = on_commission_ok,
        .commissioning_failure_callback = on_commission_fail,
    };
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);
    esp_err_t err = esp_matter::controller::pairing_code_thread(
        s_pair_node, payload, s_dataset, s_dataset_len);
    esp_matter::lock::chip_stack_unlock();
    if (err != ESP_OK) {
        s_pairing = false;
        set_pair_msg("That setup code was not accepted");
        return send_error(req, "That setup code was not accepted");
    }
    return send_json(req, "{\"ok\":true}");
}

extern "C" esp_err_t portal_forget_post(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Request is too large");
    }
    uint64_t node_id = 0;
    if (!json_find_u64(body, "node_id", node_id) || node_id == 0) {
        return send_error(req, "Node id is required");
    }
    esp_err_t err = gateway_nodes_forget(node_id);
    if (err != ESP_OK) {
        return send_error(req, "Sensor was not in the local list");
    }
    return send_json(req, "{\"ok\":true}");
}

bool parse_button_edits(const char *body, gateway_button_edit_t *out, uint8_t &count)
{
    count = 0;
    const char *mark = strstr(body, "\"buttons\"");
    if (!mark) {
        return false;
    }
    const char *p = strchr(mark, '[');
    if (!p) {
        return false;
    }
    ++p;
    while (*p && count < 6) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') {
            ++p;
        }
        if (*p == ']' || *p == 0) {
            break;
        }
        if (*p != '{') {
            break;
        }
        const char *end = strchr(p, '}');
        if (!end) {
            break;
        }
        char chunk[160];
        size_t n = static_cast<size_t>(end - p);
        if (n >= sizeof(chunk)) {
            n = sizeof(chunk) - 1;
        }
        memcpy(chunk, p, n);
        chunk[n] = 0;
        out[count].name[0] = 0;
        out[count].mode = 0;
        json_find_string(chunk, "name", out[count].name, sizeof(out[count].name));
        uint64_t mode = 0;
        if (json_find_u64(chunk, "mode", mode)) {
            out[count].mode = mode ? 1 : 0;
        }
        ++count;
        p = end + 1;
    }
    return true;
}

extern "C" esp_err_t portal_update_post(httpd_req_t *req)
{
    char body[1024];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Request is too large");
    }
    uint64_t node_id = 0;
    char name[24];
    char kind[24];
    if (!json_find_u64(body, "node_id", node_id) || node_id == 0) {
        return send_error(req, "Node id is required");
    }
    if (!json_find_string(body, "name", name, sizeof(name))) {
        name[0] = 0;
    }
    if (!json_find_string(body, "kind", kind, sizeof(kind))) {
        kind[0] = 0;
    }
    esp_err_t err = gateway_nodes_update(node_id, name, kind);
    if (err == ESP_ERR_NOT_FOUND) {
        return send_error(req, "Sensor was not in the local list");
    }
    if (err != ESP_OK) {
        return send_error(req, "Could not update that device");
    }
    gateway_button_edit_t buttons[6];
    uint8_t button_count = 0;
    if (parse_button_edits(body, buttons, button_count) && button_count > 0) {
        gateway_nodes_set_buttons(node_id, buttons, button_count);
    }
    return send_json(req, "{\"ok\":true}");
}

extern "C" esp_err_t portal_read_post(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Request is too large");
    }
    uint64_t node_id = 0;
    if (!json_find_u64(body, "node_id", node_id) || node_id == 0) {
        return send_error(req, "Node id is required");
    }
    esp_err_t err = gateway_nodes_read_now(node_id);
    if (err != ESP_OK) {
        return send_error(req, "Sensor was not in the local list");
    }
    return send_json(req, "{\"ok\":true}");
}

extern "C" esp_err_t portal_sniff_get(httpd_req_t *req)
{
    const uint32_t seq = matter_sniff_seq();
    char qbuf[48];
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        char since_s[16];
        if (httpd_query_key_value(qbuf, "since", since_s, sizeof(since_s)) == ESP_OK &&
            strtoul(since_s, nullptr, 10) == seq) {
            char tiny[72];
            snprintf(tiny, sizeof(tiny), "{\"ok\":true,\"seq\":%lu,\"unchanged\":true}",
                     static_cast<unsigned long>(seq));
            return send_json(req, tiny);
        }
    }

    static matter_sniff_entry_t entries[48];
    static char json[12288];
    size_t n = matter_sniff_copy(entries, 48);
    size_t pos = static_cast<size_t>(
        snprintf(json, sizeof(json), "{\"ok\":true,\"seq\":%lu,\"entries\":[",
                 static_cast<unsigned long>(seq)));
    for (size_t i = 0; i < n && pos + 200 < sizeof(json); ++i) {
        char text[80];
        json_escape(entries[i].text, text, sizeof(text));
        const char *dir = entries[i].dir == MATTER_SNIFF_TX ? "TX"
                          : entries[i].dir == MATTER_SNIFF_EVT ? "EVT"
                                                               : "RX";
        int wrote = snprintf(
            json + pos, sizeof(json) - pos,
            "%s{\"t\":%lld,\"dir\":\"%s\",\"type\":0,\"node\":%" PRIu64 ",\"cluster\":%lu,"
            "\"attr\":%lu,\"group\":0,\"src_ep\":%u,\"dst_ep\":%u,\"rssi\":0,\"status\":0,"
            "\"hex\":\"%s\"}",
            i ? "," : "", static_cast<long long>(entries[i].ms), dir, entries[i].node,
            static_cast<unsigned long>(entries[i].cluster),
            static_cast<unsigned long>(entries[i].attr), entries[i].endpoint,
            entries[i].endpoint, text);
        if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(json) - pos) {
            break;
        }
        pos += static_cast<size_t>(wrote);
    }
    snprintf(json + pos, sizeof(json) - pos, "]}");
    return send_json(req, json);
}

extern "C" esp_err_t portal_sniff_clear(httpd_req_t *req)
{
    matter_sniff_clear();
    return send_json(req, "{\"ok\":true}");
}

extern "C" esp_err_t portal_wifi_scan_get(httpd_req_t *req)
{
    wifi_scan_config_t scan = {};
    scan.show_hidden = true;
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        return send_error(req, "Scan failed");
    }
    uint16_t count = 12;
    wifi_ap_record_t records[12];
    esp_wifi_scan_get_ap_records(&count, records);

    static char json[2048];
    size_t used = 0;
    int wrote = snprintf(json, sizeof(json), "{\"aps\":[");
    used = static_cast<size_t>(wrote);
    for (uint16_t i = 0; i < count; ++i) {
        char ssid[33] = {};
        char ssid_e[70];
        memcpy(ssid, records[i].ssid, sizeof(ssid) - 1);
        json_escape(ssid, ssid_e, sizeof(ssid_e));
        wrote = snprintf(
            json + used, sizeof(json) - used,
            "%s{\"ssid\":\"%s\",\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"rssi\":%d}",
            i ? "," : "", ssid_e, records[i].bssid[0], records[i].bssid[1],
            records[i].bssid[2], records[i].bssid[3], records[i].bssid[4],
            records[i].bssid[5], records[i].rssi);
        if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(json) - used) {
            break;
        }
        used += static_cast<size_t>(wrote);
    }
    snprintf(json + used, sizeof(json) - used, "]}");
    return send_json(req, json);
}

extern "C" esp_err_t portal_wifi_post(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Request is too large");
    }
    char ssid[33];
    char password[65];
    if (!json_find_string(body, "ssid", ssid, sizeof(ssid)) || !ssid[0]) {
        return send_error(req, "SSID is required");
    }
    if (!json_find_string(body, "password", password, sizeof(password))) {
        password[0] = 0;
    }
    wifi_config_t cfg = {};
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, password, strlen(password));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        return send_error(req, "Could not start the connection");
    }
    return send_json(req, "{\"ok\":true}");
}

static bool peer_token(httpd_req_t *req)
{
    char token[48];
    if (httpd_req_get_hdr_value_str(req, "X-Peer-Token", token, sizeof(token)) != ESP_OK) {
        return false;
    }
    return peer_link_token_ok(token);
}

static esp_err_t peer_denied(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    return send_json(req, "{\"ok\":false,\"error\":\"Not linked\"}");
}

extern "C" esp_err_t portal_peer_get(httpd_req_t *req)
{
    char *json = static_cast<char *>(malloc(3072));
    if (!json) {
        return send_error(req, "Link status is unavailable");
    }
    int n = peer_link_status_json(json, 3072);
    esp_err_t err = (n < 0) ? send_error(req, "Link status is unavailable") : send_json(req, json);
    free(json);
    return err;
}

extern "C" esp_err_t portal_peer_devices_get(httpd_req_t *req)
{
    if (!peer_token(req)) {
        return peer_denied(req);
    }
    char *json = static_cast<char *>(malloc(3072));
    if (!json) {
        return send_error(req, "Device list is unavailable");
    }
    int n = peer_link_write_local_devices(json, 3072);
    esp_err_t err = (n < 0) ? send_error(req, "Device list is unavailable") : send_json(req, json);
    free(json);
    return err;
}

static esp_err_t peer_result(httpd_req_t *req, esp_err_t err, const char *fail)
{
    if (err != ESP_OK) {
        return send_error(req, fail);
    }
    return send_json(req, "{\"ok\":true}");
}

extern "C" esp_err_t portal_peer_discover_post(httpd_req_t *req)
{
    return peer_result(req, peer_link_discover(), "Could not look for a gateway");
}

extern "C" esp_err_t portal_peer_pair_post(httpd_req_t *req)
{
    char body[160];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "Address is required");
    }
    char host[48];
    if (!json_find_string(body, "host", host, sizeof(host)) || !host[0]) {
        return send_error(req, "Address is required");
    }
    esp_err_t err = peer_link_pair(host);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "Already linked, or this gateway has no IP yet");
    }
    return peer_result(req, err, "The other gateway did not answer");
}

extern "C" esp_err_t portal_peer_accept_post(httpd_req_t *req)
{
    return peer_result(req, peer_link_accept(), "Could not accept the link");
}

extern "C" esp_err_t portal_peer_decline_post(httpd_req_t *req)
{
    return peer_result(req, peer_link_decline(), "Could not decline");
}

extern "C" esp_err_t portal_peer_unpair_post(httpd_req_t *req)
{
    return peer_result(req, peer_link_unpair(), "Could not unpair");
}

static esp_err_t read_peer_body(httpd_req_t *req, char *body, size_t len, char *id, size_t id_len,
                                char *name, char *role, char *host, char *token)
{
    if (read_body(req, body, len) != ESP_OK) {
        return ESP_FAIL;
    }
    json_find_string(body, "id", id, id_len);
    json_find_string(body, "name", name, 40);
    json_find_string(body, "role", role, 12);
    json_find_string(body, "host", host, 48);
    json_find_string(body, "token", token, 40);
    return ESP_OK;
}

extern "C" esp_err_t portal_peer_invite_post(httpd_req_t *req)
{
    char body[320], id[20], name[40], role[12], host[48], token[40];
    name[0] = role[0] = host[0] = token[0] = id[0] = 0;
    if (read_peer_body(req, body, sizeof(body), id, sizeof(id), name, role, host, token) != ESP_OK) {
        return send_error(req, "Invite was incomplete");
    }
    esp_err_t err = peer_link_on_invite(id, name, role, host, token);
    if (err != ESP_OK) {
        return send_error(req, "This gateway is already linked");
    }
    return send_json(req, "{\"ok\":true,\"name\":\"ICC Thread Gateway\",\"role\":\"thread\"}");
}

extern "C" esp_err_t portal_peer_confirm_post(httpd_req_t *req)
{
    char body[320], id[20], name[40], role[12], host[48], token[40];
    id[0] = name[0] = role[0] = host[0] = token[0] = 0;
    if (read_peer_body(req, body, sizeof(body), id, sizeof(id), name, role, host, token) != ESP_OK) {
        return send_error(req, "Confirmation was incomplete");
    }
    char hdr[48];
    if (httpd_req_get_hdr_value_str(req, "X-Peer-Token", hdr, sizeof(hdr)) == ESP_OK && hdr[0]) {
        snprintf(token, sizeof(token), "%s", hdr);
    }
    return peer_result(req, peer_link_on_confirm(id, name, role, host, token),
                       "This link request is no longer waiting");
}

extern "C" esp_err_t portal_peer_bye_post(httpd_req_t *req)
{
    char token[48];
    token[0] = 0;
    if (httpd_req_get_hdr_value_str(req, "X-Peer-Token", token, sizeof(token)) != ESP_OK) {
        char body[120];
        if (read_body(req, body, sizeof(body)) == ESP_OK) {
            json_find_string(body, "token", token, sizeof(token));
        }
    }
    return peer_result(req, peer_link_on_bye(token), "Not linked");
}

extern "C" esp_err_t portal_peer_set_post(httpd_req_t *req)
{
    if (!peer_token(req)) {
        return peer_denied(req);
    }
    char body[120], id[24];
    if (read_body(req, body, sizeof(body)) != ESP_OK ||
        !json_find_string(body, "id", id, sizeof(id)) || !id[0]) {
        return send_error(req, "Device id is required");
    }
    bool on = strstr(body, "\"on\":true") != nullptr;
    return peer_result(req, peer_link_set_local(id, on), "Could not reach that device");
}

extern "C" esp_err_t portal_peer_command_post(httpd_req_t *req)
{
    char body[120], id[24];
    if (read_body(req, body, sizeof(body)) != ESP_OK ||
        !json_find_string(body, "id", id, sizeof(id)) || !id[0]) {
        return send_error(req, "Device id is required");
    }
    bool on = strstr(body, "\"on\":true") != nullptr;
    return peer_result(req, peer_link_set_remote(id, on), "The other gateway did not accept the command");
}

extern "C" esp_err_t portal_homekit_reset_post(httpd_req_t *req)
{
    esp_err_t err = homekit_bridge_reset_pairings();
    if (err != ESP_OK) {
        return send_error(req, "HomeKit is not ready");
    }
    return send_json(req, "{\"ok\":true}");
}

esp_err_t portal_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    /* 16384 does not fit in internal RAM (httpd_start returns ESP_ERR_HTTPD_TASK). */
    cfg.stack_size = 12288;
    cfg.max_uri_handlers = 28;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 15;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Portal start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = portal_root_get},
        {.uri = "/api/status", .method = HTTP_GET, .handler = portal_status_get},
        {.uri = "/api/nodes/pair", .method = HTTP_POST, .handler = portal_pair_post},
        {.uri = "/api/nodes/forget", .method = HTTP_POST, .handler = portal_forget_post},
        {.uri = "/api/nodes/update", .method = HTTP_POST, .handler = portal_update_post},
        {.uri = "/api/nodes/read", .method = HTTP_POST, .handler = portal_read_post},
        {.uri = "/api/matter/sniff", .method = HTTP_GET, .handler = portal_sniff_get},
        {.uri = "/api/matter/sniff/clear", .method = HTTP_POST, .handler = portal_sniff_clear},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = portal_wifi_scan_get},
        {.uri = "/api/wifi/sta", .method = HTTP_POST, .handler = portal_wifi_post},
        {.uri = "/api/homekit/reset", .method = HTTP_POST, .handler = portal_homekit_reset_post},
        {.uri = "/api/peer", .method = HTTP_GET, .handler = portal_peer_get},
        {.uri = "/api/peer/devices", .method = HTTP_GET, .handler = portal_peer_devices_get},
        {.uri = "/api/peer/discover", .method = HTTP_POST, .handler = portal_peer_discover_post},
        {.uri = "/api/peer/pair", .method = HTTP_POST, .handler = portal_peer_pair_post},
        {.uri = "/api/peer/accept", .method = HTTP_POST, .handler = portal_peer_accept_post},
        {.uri = "/api/peer/decline", .method = HTTP_POST, .handler = portal_peer_decline_post},
        {.uri = "/api/peer/unpair", .method = HTTP_POST, .handler = portal_peer_unpair_post},
        {.uri = "/api/peer/invite", .method = HTTP_POST, .handler = portal_peer_invite_post},
        {.uri = "/api/peer/confirm", .method = HTTP_POST, .handler = portal_peer_confirm_post},
        {.uri = "/api/peer/bye", .method = HTTP_POST, .handler = portal_peer_bye_post},
        {.uri = "/api/peer/set", .method = HTTP_POST, .handler = portal_peer_set_post},
        {.uri = "/api/peer/command", .method = HTTP_POST, .handler = portal_peer_command_post},
    };
    for (const httpd_uri_t &route : routes) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &route), TAG,
                            "Cannot register %s", route.uri);
    }

    char ip[20] = "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr) {
        esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));
    }
    ESP_LOGI(TAG, "Portal http://%s/", ip[0] ? ip : "192.168.20.198");
    return ESP_OK;
}
