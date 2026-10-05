#include "homekit_bridge.h"

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

extern "C" int hap_platform_httpd_reserve_stack(void);
extern "C" void hap_platform_httpd_release_stack(void);
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_nodes.h"
#include "hap.h"
#include "matter_sniff.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

namespace {

const char *TAG = "homekit";
constexpr char kSetupCode[] = "111-22-333";
constexpr char kSetupId[] = "THRD";
constexpr int kMax = 8;

struct slot_t {
    bool used;
    uint64_t node_id;
    gateway_kind_t kind;
    hap_acc_t *acc;
    hap_char_t *on;
    hap_char_t *level;
    hap_char_t *hue;
    hap_char_t *sat;
    hap_char_t *ct;
    hap_char_t *contact;
    hap_char_t *motion;
    hap_char_t *leak;
    hap_char_t *temp;
    hap_char_t *hum;
    hap_char_t *co2;
    hap_char_t *pm;
    hap_char_t *air;
    hap_char_t *shade_cur;
    hap_char_t *shade_tgt;
    hap_char_t *button;
    hap_char_t *battery;
    uint8_t button_count;
    uint8_t btn_mode[6];
    uint16_t btn_ep[6];
    hap_char_t *btn_event[6];
    hap_char_t *btn_on[6];
    uint32_t seen_press_n[6];
    char btn_label[6][24];
    struct hook_t {
        uint64_t node_id;
        uint8_t index;
    } btn_hook[6];
    hap_char_t *occ;
    hap_char_t *lux;
    hap_char_t *lock_cur;
    hap_char_t *lock_tgt;
    hap_char_t *thermo_mode;
    hap_char_t *thermo_tgt;
    hap_char_t *thermo_set;
    hap_char_t *smoke;
    hap_char_t *co;
    uint8_t thermo_hap_mode;
    float heat_set_c;
    char name[32];
    char serial[24];
    float hue_deg;
    float sat_pct;
    uint32_t seen_presses;
    uint32_t note_valid;
    bool note_contact;
    bool note_motion;
    bool note_leak;
};

slot_t s_slots[kMax];
hap_acc_t *s_bridge;
SemaphoreHandle_t s_mu;
bool s_started;
homekit_bridge_status_t s_status;

int identify(hap_acc_t *acc)
{
    (void)acc;
    return HAP_SUCCESS;
}

void lock()
{
    if (s_mu) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
    }
}

void unlock()
{
    if (s_mu) {
        xSemaphoreGive(s_mu);
    }
}

slot_t *find_slot(uint64_t node_id)
{
    for (slot_t &slot : s_slots) {
        if (slot.used && slot.node_id == node_id) {
            return &slot;
        }
    }
    return nullptr;
}

slot_t *alloc_slot()
{
    for (slot_t &slot : s_slots) {
        if (!slot.used) {
            memset(&slot, 0, sizeof(slot));
            slot.used = true;
            return &slot;
        }
    }
    return nullptr;
}

bool value_changed(hap_char_t *hc, const hap_val_t &next, hap_char_format_t format)
{
    const hap_val_t *cur = hap_char_get_val(hc);
    if (!cur) {
        return true;
    }
    switch (format) {
    case HAP_CHAR_FORMAT_BOOL:
        return cur->b != next.b;
    case HAP_CHAR_FORMAT_FLOAT:
        return fabsf(cur->f - next.f) >= 0.05f;
    default:
        return cur->i != next.i;
    }
}

bool set_int(hap_char_t *hc, int value)
{
    if (!hc) {
        return false;
    }
    hap_val_t val = {.i = value};
    if (!value_changed(hc, val, HAP_CHAR_FORMAT_INT)) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    hap_char_update_val(hc, &val);
    return true;
}

bool set_u8(hap_char_t *hc, uint8_t value)
{
    if (!hc) {
        return false;
    }
    hap_val_t val = {.u = value};
    if (!value_changed(hc, val, HAP_CHAR_FORMAT_UINT8)) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    hap_char_update_val(hc, &val);
    return true;
}

/** Contact, leak, occupancy: same push the zigbee bridge uses. Re-arm every
 * live session, and if Home already holds this value, nudge the opposite
 * edge so the open app still gets an EVENT. */
bool set_u8_notify(hap_char_t *hc, uint8_t value, bool again)
{
    (void)again;
    if (!hc) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    const hap_val_t *cur = hap_char_get_val(hc);
    if (cur && static_cast<uint8_t>(cur->u) == value) {
        hap_val_t opp = {.u = static_cast<uint8_t>(value ? 0 : 1)};
        hap_char_update_val(hc, &opp);
    }
    hap_val_t val = {.u = value};
    hap_char_update_val(hc, &val);
    return true;
}

bool set_bool(hap_char_t *hc, bool value)
{
    if (!hc) {
        return false;
    }
    hap_val_t val = {.b = value};
    if (!value_changed(hc, val, HAP_CHAR_FORMAT_BOOL)) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    hap_char_update_val(hc, &val);
    return true;
}

bool set_bool_notify(hap_char_t *hc, bool value, bool again)
{
    (void)again;
    if (!hc) {
        return false;
    }
    hap_val_t val = {.b = value};
    if (!value_changed(hc, val, HAP_CHAR_FORMAT_BOOL)) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    hap_char_update_val(hc, &val);
    return true;
}

/** Stateful remote buttons. Force an edge when the stored value already
 * matches, so a press still reaches an open Home session. */
bool set_bool_notify_edge(hap_char_t *hc, bool value)
{
    if (!hc) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    const hap_val_t *cur = hap_char_get_val(hc);
    if (cur && cur->b == value) {
        hap_val_t opp = {.b = !value};
        hap_char_update_val(hc, &opp);
    }
    hap_val_t val = {.b = value};
    hap_char_update_val(hc, &val);
    return true;
}

bool set_float(hap_char_t *hc, float value)
{
    if (!hc) {
        return false;
    }
    hap_val_t val = {.f = value};
    if (!value_changed(hc, val, HAP_CHAR_FORMAT_FLOAT)) {
        return false;
    }
    hap_char_enable_notif_all_sessions(hc);
    hap_char_update_val(hc, &val);
    return true;
}

uint8_t air_quality_from_pm(float pm)
{
    if (pm < 12) {
        return 1;
    }
    if (pm < 35) {
        return 2;
    }
    if (pm < 55) {
        return 3;
    }
    if (pm < 150) {
        return 4;
    }
    return 5;
}

const char *model_for(gateway_kind_t kind)
{
    switch (kind) {
    case GW_KIND_CONTACT:
        return "Contact sensor";
    case GW_KIND_MOTION:
        return "Motion sensor";
    case GW_KIND_LEAK:
        return "Leak sensor";
    case GW_KIND_CLIMATE:
        return "Climate sensor";
    case GW_KIND_AIR:
        return "Air quality";
    case GW_KIND_PLUG:
        return "Plug";
    case GW_KIND_LIGHT:
        return "Dimmable light";
    case GW_KIND_LIGHT_CT:
        return "White spectrum light";
    case GW_KIND_LIGHT_COLOR:
        return "Color light";
    case GW_KIND_BUTTON:
        return "Button";
    case GW_KIND_SHADE:
        return "Window covering";
    case GW_KIND_ONOFF:
        return "On/Off light";
    case GW_KIND_OCCUPANCY:
        return "Occupancy sensor";
    case GW_KIND_LUX:
        return "Light sensor";
    case GW_KIND_LOCK:
        return "Door lock";
    case GW_KIND_THERMOSTAT:
        return "Thermostat";
    case GW_KIND_FAN:
        return "Fan";
    case GW_KIND_SMOKE:
        return "Smoke sensor";
    case GW_KIND_SWITCH:
        return "Switch";
    default:
        return "Matter";
    }
}

hap_cid_t category_for(gateway_kind_t kind)
{
    switch (kind) {
    case GW_KIND_LIGHT:
    case GW_KIND_LIGHT_CT:
    case GW_KIND_LIGHT_COLOR:
    case GW_KIND_ONOFF:
        return HAP_CID_LIGHTING;
    case GW_KIND_PLUG:
        return HAP_CID_OUTLET;
    case GW_KIND_BUTTON:
        return HAP_CID_PROGRAMMABLE_SWITCH;
    case GW_KIND_SHADE:
        return HAP_CID_WINDOW_COVERING;
    case GW_KIND_LOCK:
        return HAP_CID_LOCK;
    case GW_KIND_THERMOSTAT:
        return HAP_CID_THERMOSTAT;
    case GW_KIND_FAN:
        return HAP_CID_FAN;
    case GW_KIND_SWITCH:
        return HAP_CID_SWITCH;
    default:
        return HAP_CID_SENSOR;
    }
}

int on_write(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    (void)write_priv;
    slot_t *slot = static_cast<slot_t *>(serv_priv);
    int result = HAP_SUCCESS;
    if (!slot) {
        return HAP_FAIL;
    }
    for (int i = 0; i < count; ++i) {
        hap_write_data_t *item = &write_data[i];
        const char *uuid = hap_char_get_type_uuid(item->hc);
        esp_err_t err = ESP_OK;
        if (!strcmp(uuid, HAP_CHAR_UUID_ON)) {
            err = slot->kind == GW_KIND_FAN ? gateway_nodes_set_fan(slot->node_id, item->val.b)
                                            : gateway_nodes_set_on(slot->node_id, item->val.b);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_BRIGHTNESS)) {
            err = gateway_nodes_set_level_pct(slot->node_id, item->val.i);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_HUE)) {
            slot->hue_deg = item->val.f;
            err = gateway_nodes_set_hue_sat(slot->node_id, slot->hue_deg, slot->sat_pct);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_SATURATION)) {
            slot->sat_pct = item->val.f;
            err = gateway_nodes_set_hue_sat(slot->node_id, slot->hue_deg, slot->sat_pct);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_COLOR_TEMPERATURE)) {
            err = gateway_nodes_set_mireds(slot->node_id, static_cast<int>(item->val.u));
        } else if (!strcmp(uuid, HAP_CHAR_UUID_TARGET_POSITION)) {
            err = gateway_nodes_set_shade_open_pct(slot->node_id, item->val.u);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_LOCK_TARGET_STATE)) {
            err = gateway_nodes_set_lock(slot->node_id, item->val.u == 1);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE)) {
            slot->thermo_hap_mode = static_cast<uint8_t>(item->val.u);
            err = gateway_nodes_set_thermo_mode(slot->node_id, slot->thermo_hap_mode);
        } else if (!strcmp(uuid, HAP_CHAR_UUID_TARGET_TEMPERATURE)) {
            slot->heat_set_c = item->val.f;
            err = gateway_nodes_set_heat_setpoint(slot->node_id, slot->heat_set_c);
        } else {
            *(item->status) = HAP_STATUS_SUCCESS;
            continue;
        }
        if (err != ESP_OK) {
            *(item->status) = HAP_STATUS_OO_RES;
            result = HAP_FAIL;
            continue;
        }
        hap_char_update_val(item->hc, &item->val);
        *(item->status) = HAP_STATUS_SUCCESS;
    }
    return result;
}

int on_button_write(hap_write_data_t write_data[], int count, void *serv_priv, void *write_priv)
{
    (void)write_priv;
    auto *hook = static_cast<slot_t::hook_t *>(serv_priv);
    if (!hook) {
        return HAP_FAIL;
    }
    for (int i = 0; i < count; ++i) {
        hap_write_data_t *item = &write_data[i];
        if (!item->hc || !item->status) {
            continue;
        }
        const char *uuid = hap_char_get_type_uuid(item->hc);
        if (uuid && !strcmp(uuid, HAP_CHAR_UUID_ON)) {
            gateway_nodes_set_button_on(hook->node_id, hook->index, item->val.b);
        }
        hap_char_update_val(item->hc, &item->val);
        *(item->status) = HAP_STATUS_SUCCESS;
    }
    return HAP_SUCCESS;
}

void add_battery(slot_t *slot, hap_acc_t *acc)
{
    hap_serv_t *battery = hap_serv_battery_service_create(100, 0, 0);
    if (!battery) {
        return;
    }
    hap_acc_add_serv(acc, battery);
    slot->battery = hap_serv_get_char_by_uuid(battery, HAP_CHAR_UUID_BATTERY_LEVEL);
}

bool build_remote(slot_t *slot, hap_acc_t *acc, const gateway_node_view_t &view)
{
    uint8_t count = view.button_count == 0 ? 1 : view.button_count;
    if (count > 6) {
        count = 6;
    }
    hap_serv_t *servs[6] = {};
    bool any_stateful = false;
    bool any_stateless = false;
    for (uint8_t i = 0; i < count; ++i) {
        const bool stateful = view.button_count > 0 && view.button_mode[i] == 1;
        any_stateful = any_stateful || stateful;
        any_stateless = any_stateless || !stateful;
        const char *label = (view.button_count > 0 && view.button_name[i][0]) ? view.button_name[i]
                                                                               : "Button";
        snprintf(slot->btn_label[i], sizeof(slot->btn_label[i]), "%s", label);
        slot->btn_mode[i] = stateful ? 1 : 0;
        slot->btn_ep[i] = view.button_count > 0 ? view.button_ep[i] : view.endpoint_id;
        slot->seen_press_n[i] = view.button_count > 0 ? view.button_press_n[i] : view.button_presses;
        slot->btn_hook[i].node_id = view.node_id;
        slot->btn_hook[i].index = i;

        hap_char_t *name_ch = hap_char_name_create(slot->btn_label[i]);
        hap_char_t *cfg_ch = hap_char_configured_name_create(slot->btn_label[i]);
        if (!name_ch || !cfg_ch) {
            if (name_ch) {
                hap_char_delete(name_ch);
            }
            if (cfg_ch) {
                hap_char_delete(cfg_ch);
            }
            return false;
        }
        hap_char_string_set_maxlen(name_ch, 23);
        hap_char_string_set_maxlen(cfg_ch, 23);

        hap_serv_t *serv = nullptr;
        if (stateful) {
            serv = hap_serv_switch_create(view.button_on[i]);
        } else {
            serv = hap_serv_stateless_programmable_switch_create(0);
        }
        if (!serv) {
            hap_char_delete(name_ch);
            hap_char_delete(cfg_ch);
            return false;
        }
        hap_serv_add_char(serv, name_ch);
        hap_serv_add_char(serv, cfg_ch);
        hap_serv_set_priv(serv, &slot->btn_hook[i]);
        hap_serv_set_write_cb(serv, on_button_write);
        if (!stateful) {
            hap_serv_add_char(serv, hap_char_service_label_index_create(static_cast<uint8_t>(i + 1)));
            hap_char_t *ev = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_PROGRAMMABLE_SWITCH_EVENT);
            static const uint8_t k_events[] = {0, 1, 2};
            if (ev) {
                hap_char_add_valid_vals(ev, k_events, sizeof(k_events));
            }
            slot->btn_event[i] = ev;
            slot->btn_on[i] = nullptr;
        } else {
            slot->btn_on[i] = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
            slot->btn_event[i] = nullptr;
        }
        hap_acc_add_serv(acc, serv);
        servs[i] = serv;
        if (i == 0) {
            hap_serv_mark_primary(serv);
        }
    }
    if (servs[0]) {
        for (uint8_t i = 1; i < count; ++i) {
            if (servs[i]) {
                hap_serv_link_serv(servs[0], servs[i]);
            }
        }
    }
    if (any_stateless && count > 1) {
        hap_serv_t *label = hap_serv_service_label_create(1);
        if (label) {
            hap_acc_add_serv(acc, label);
        }
    }
    (void)any_stateful;
    add_battery(slot, acc);
    slot->button_count = count;
    return true;
}

bool build_services(slot_t *slot, hap_acc_t *acc, const gateway_node_view_t &view)
{
    if (view.kind == GW_KIND_BUTTON) {
        return build_remote(slot, acc, view);
    }
    hap_serv_t *serv = nullptr;
    switch (view.kind) {
    case GW_KIND_CONTACT:
        serv = hap_serv_contact_sensor_create(view.contact_open ? 1 : 0);
        break;
    case GW_KIND_MOTION:
        serv = hap_serv_motion_sensor_create(view.motion);
        break;
    case GW_KIND_LEAK:
        serv = hap_serv_leak_sensor_create(view.leak ? 1 : 0);
        break;
    case GW_KIND_CLIMATE:
        serv = hap_serv_temperature_sensor_create(view.temp_c);
        break;
    case GW_KIND_AIR:
        serv = hap_serv_air_quality_sensor_create(air_quality_from_pm(view.pm25));
        break;
    case GW_KIND_PLUG:
        serv = hap_serv_outlet_create(view.on, view.power_w > 1.0f);
        break;
    case GW_KIND_LIGHT:
    case GW_KIND_LIGHT_CT:
    case GW_KIND_LIGHT_COLOR:
        serv = hap_serv_lightbulb_create(view.on);
        break;
    case GW_KIND_BUTTON:
        return false;
    case GW_KIND_SHADE:
        serv = hap_serv_window_covering_create(view.shade_open_pct, view.shade_open_pct, 2);
        break;
    case GW_KIND_ONOFF:
        serv = hap_serv_lightbulb_create(view.on);
        break;
    case GW_KIND_SWITCH:
        serv = hap_serv_switch_create(view.on);
        break;
    case GW_KIND_OCCUPANCY:
        serv = hap_serv_occupancy_sensor_create(view.motion ? 1 : 0);
        break;
    case GW_KIND_LUX:
        serv = hap_serv_light_sensor_create(view.lux > 0.0001f ? view.lux : 0.0001f);
        break;
    case GW_KIND_LOCK:
        serv = hap_serv_lock_mechanism_create(view.lock_secured ? 1 : 0, view.lock_secured ? 1 : 0);
        break;
    case GW_KIND_THERMOSTAT:
        serv = hap_serv_thermostat_create(view.thermo_mode, view.thermo_mode, view.temp_c,
                                          view.heat_set_c > 0 ? view.heat_set_c : 21.0f, 0);
        break;
    case GW_KIND_FAN:
        serv = hap_serv_fan_create(view.on);
        break;
    case GW_KIND_SMOKE:
        serv = hap_serv_smoke_sensor_create(view.smoke ? 1 : 0);
        break;
    default:
        return false;
    }
    if (!serv) {
        return false;
    }
    hap_serv_add_char(serv, hap_char_name_create(slot->name));
    hap_serv_set_priv(serv, slot);
    if (view.kind == GW_KIND_LIGHT || view.kind == GW_KIND_LIGHT_CT ||
        view.kind == GW_KIND_LIGHT_COLOR || view.kind == GW_KIND_PLUG ||
        view.kind == GW_KIND_SHADE || view.kind == GW_KIND_ONOFF ||
        view.kind == GW_KIND_SWITCH || view.kind == GW_KIND_FAN ||
        view.kind == GW_KIND_LOCK || view.kind == GW_KIND_THERMOSTAT) {
        hap_serv_set_write_cb(serv, on_write);
    }
    hap_acc_add_serv(acc, serv);

    if (view.kind == GW_KIND_CONTACT) {
        hap_serv_mark_primary(serv);
        slot->contact = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CONTACT_SENSOR_STATE);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_MOTION) {
        hap_serv_mark_primary(serv);
        slot->motion = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_MOTION_DETECTED);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_LEAK) {
        slot->leak = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_LEAK_DETECTED);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_CLIMATE) {
        slot->temp = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
        hap_serv_t *hum = hap_serv_humidity_sensor_create(view.humidity_pct);
        if (hum) {
            hap_acc_add_serv(acc, hum);
            slot->hum = hap_serv_get_char_by_uuid(hum, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY);
        }
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_AIR) {
        slot->air = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_AIR_QUALITY);
        hap_serv_add_char(serv, hap_char_pm_2_5_density_create(view.pm25));
        slot->pm = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_PM_2_5_DENSITY);
        hap_serv_t *co2 = hap_serv_carbon_dioxide_sensor_create(view.co2_ppm > 1200 ? 1 : 0);
        if (co2) {
            hap_serv_add_char(co2, hap_char_carbon_dioxide_level_create(view.co2_ppm));
            hap_acc_add_serv(acc, co2);
            slot->co2 = hap_serv_get_char_by_uuid(co2, HAP_CHAR_UUID_CARBON_DIOXIDE_LEVEL);
        }
        hap_serv_t *temp = hap_serv_temperature_sensor_create(view.temp_c);
        if (temp) {
            hap_acc_add_serv(acc, temp);
            slot->temp = hap_serv_get_char_by_uuid(temp, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
        }
        hap_serv_t *hum = hap_serv_humidity_sensor_create(view.humidity_pct);
        if (hum) {
            hap_acc_add_serv(acc, hum);
            slot->hum = hap_serv_get_char_by_uuid(hum, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY);
        }
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_PLUG) {
        slot->on = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
    } else if (view.kind == GW_KIND_LIGHT || view.kind == GW_KIND_LIGHT_CT ||
               view.kind == GW_KIND_LIGHT_COLOR) {
        hap_serv_add_char(serv, hap_char_brightness_create(view.level_pct));
        slot->on = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
        slot->level = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_BRIGHTNESS);
        if (view.kind == GW_KIND_LIGHT_CT || view.kind == GW_KIND_LIGHT_COLOR) {
            hap_serv_add_char(serv, hap_char_color_temperature_create(view.mireds ? view.mireds : 250));
            slot->ct = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_COLOR_TEMPERATURE);
        }
        if (view.kind == GW_KIND_LIGHT_COLOR) {
            hap_serv_add_char(serv, hap_char_hue_create(view.hue_deg));
            hap_serv_add_char(serv, hap_char_saturation_create(view.saturation_pct));
            slot->hue = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_HUE);
            slot->sat = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_SATURATION);
        }
    } else if (view.kind == GW_KIND_SHADE) {
        slot->shade_cur = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CURRENT_POSITION);
        slot->shade_tgt = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_TARGET_POSITION);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_ONOFF || view.kind == GW_KIND_SWITCH ||
               view.kind == GW_KIND_FAN) {
        slot->on = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
    } else if (view.kind == GW_KIND_OCCUPANCY) {
        slot->occ = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_OCCUPANCY_DETECTED);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_LUX) {
        slot->lux = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CURRENT_AMBIENT_LIGHT_LEVEL);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_LOCK) {
        slot->lock_cur = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_LOCK_CURRENT_STATE);
        slot->lock_tgt = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_LOCK_TARGET_STATE);
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_THERMOSTAT) {
        slot->thermo_mode = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
        slot->thermo_tgt = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
        slot->temp = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
        slot->thermo_set = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_TARGET_TEMPERATURE);
        slot->thermo_hap_mode = view.thermo_mode;
        slot->heat_set_c = view.heat_set_c > 0 ? view.heat_set_c : 21.0f;
        add_battery(slot, acc);
    } else if (view.kind == GW_KIND_SMOKE) {
        slot->smoke = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_SMOKE_DETECTED);
        hap_serv_t *co = hap_serv_carbon_monoxide_sensor_create(view.co_alarm ? 1 : 0);
        if (co) {
            hap_acc_add_serv(acc, co);
            slot->co = hap_serv_get_char_by_uuid(co, HAP_CHAR_UUID_CARBON_MONOXIDE_DETECTED);
        }
        add_battery(slot, acc);
    }
    return true;
}

void create_slot(const gateway_node_view_t &view)
{
    if (find_slot(view.node_id) || !s_bridge) {
        return;
    }
    slot_t *slot = alloc_slot();
    if (!slot) {
        return;
    }
    slot->node_id = view.node_id;
    slot->kind = view.kind;
    slot->hue_deg = view.hue_deg;
    slot->sat_pct = view.saturation_pct;
    snprintf(slot->name, sizeof(slot->name), "%s", view.name[0] ? view.name : model_for(view.kind));
    snprintf(slot->serial, sizeof(slot->serial), "%llu", static_cast<unsigned long long>(view.node_id));
    char aid_key[32];
    snprintf(aid_key, sizeof(aid_key), "node-%llu", static_cast<unsigned long long>(view.node_id));

    hap_acc_cfg_t cfg = {
        .name = slot->name,
        .model = const_cast<char *>(model_for(view.kind)),
        .manufacturer = const_cast<char *>("Matter"),
        .serial_num = slot->serial,
        .fw_rev = const_cast<char *>("1.0.0"),
        .hw_rev = const_cast<char *>("1.0"),
        .pv = const_cast<char *>("1.1.0"),
        .cid = category_for(view.kind),
        .identify_routine = identify,
    };
    if (view.kind == GW_KIND_BUTTON) {
        bool any_stateful = false;
        for (uint8_t i = 0; i < view.button_count; ++i) {
            if (view.button_mode[i] == 1) {
                any_stateful = true;
            }
        }
        cfg.cid = any_stateful ? HAP_CID_SWITCH : HAP_CID_PROGRAMMABLE_SWITCH;
    }
    hap_acc_t *acc = hap_acc_create(&cfg);
    if (!acc || !build_services(slot, acc, view)) {
        if (acc) {
            hap_acc_delete(acc);
        }
        memset(slot, 0, sizeof(*slot));
        return;
    }
    slot->acc = acc;
    slot->seen_presses = view.button_presses;
    if (view.valid & GW_VALID_CONTACT) {
        slot->note_contact = view.contact_open;
        slot->note_valid |= GW_VALID_CONTACT;
    }
    if (view.valid & GW_VALID_MOTION) {
        slot->note_motion = view.motion;
        slot->note_valid |= GW_VALID_MOTION;
    }
    if (view.valid & GW_VALID_LEAK) {
        slot->note_leak = view.leak;
        slot->note_valid |= GW_VALID_LEAK;
    }
    hap_add_bridged_accessory(acc, hap_get_unique_aid(aid_key));
    s_status.accessory_count++;
    if (s_started) {
        hap_update_config_number();
    }
    ESP_LOGI(TAG, "Bridged %s '%s'", model_for(view.kind), slot->name);
}

void apply_view(slot_t *slot, const gateway_node_view_t &view)
{
    /* Value changes are delivered as EVENT/1.0 on the controller's existing
     * pair-verify socket. Closing that socket forces Home to wait for a fresh
     * GET, which is the force-quit path. */
    bool pushed = false;
    if (view.valid & GW_VALID_ON) {
        pushed = set_bool(slot->on, view.on) || pushed;
    }
    if (view.valid & GW_VALID_LEVEL) {
        pushed = set_int(slot->level, view.level_pct) || pushed;
    }
    if (view.valid & GW_VALID_COLOR) {
        slot->hue_deg = view.hue_deg;
        slot->sat_pct = view.saturation_pct;
        pushed = set_float(slot->hue, view.hue_deg) || pushed;
        pushed = set_float(slot->sat, view.saturation_pct) || pushed;
    }
    if ((view.valid & GW_VALID_MIREDS) && slot->ct) {
        hap_val_t val = {.u = view.mireds};
        if (value_changed(slot->ct, val, HAP_CHAR_FORMAT_UINT16)) {
            hap_char_enable_notif_all_sessions(slot->ct);
            hap_char_update_val(slot->ct, &val);
            pushed = true;
        }
    }
    if (view.valid & GW_VALID_CONTACT) {
        const bool again = (slot->note_valid & GW_VALID_CONTACT) != 0;
        if (!again || slot->note_contact != view.contact_open) {
            pushed = set_u8_notify(slot->contact, view.contact_open ? 1 : 0, again) || pushed;
            matter_sniff_push(MATTER_SNIFF_EVT, view.node_id, 0x0045, view.endpoint_id,
                              view.contact_open ? 1 : 0, view.contact_open ? "homekit open" : "homekit closed");
            slot->note_contact = view.contact_open;
            slot->note_valid |= GW_VALID_CONTACT;
        }
    }
    if (view.valid & GW_VALID_MOTION && view.kind != GW_KIND_CONTACT) {
        const bool again = (slot->note_valid & GW_VALID_MOTION) != 0;
        if (!again || slot->note_motion != view.motion) {
            pushed = set_bool_notify(slot->motion, view.motion, again) || pushed;
            pushed = set_u8_notify(slot->occ, view.motion ? 1 : 0, again) || pushed;
            matter_sniff_push(MATTER_SNIFF_EVT, view.node_id, 0x0406, view.endpoint_id,
                              view.motion ? 1 : 0, view.motion ? "homekit detected" : "homekit clear");
            slot->note_motion = view.motion;
            slot->note_valid |= GW_VALID_MOTION;
        }
    }
    if (view.valid & GW_VALID_LEAK) {
        const bool again = (slot->note_valid & GW_VALID_LEAK) != 0;
        if (!again || slot->note_leak != view.leak) {
            pushed = set_u8_notify(slot->leak, view.leak ? 1 : 0, again) || pushed;
            matter_sniff_push(MATTER_SNIFF_EVT, view.node_id, 0x0045, view.endpoint_id,
                              view.leak ? 1 : 0, view.leak ? "homekit leak" : "homekit dry");
            slot->note_leak = view.leak;
            slot->note_valid |= GW_VALID_LEAK;
        }
    }
    if (view.valid & GW_VALID_TEMP) {
        pushed = set_float(slot->temp, view.temp_c) || pushed;
    }
    if (view.valid & GW_VALID_HUMIDITY) {
        pushed = set_float(slot->hum, view.humidity_pct) || pushed;
    }
    if (view.valid & GW_VALID_CO2) {
        pushed = set_float(slot->co2, view.co2_ppm) || pushed;
    }
    if (view.valid & GW_VALID_PM25) {
        pushed = set_float(slot->pm, view.pm25) || pushed;
        pushed = set_u8(slot->air, air_quality_from_pm(view.pm25)) || pushed;
    }
    if (view.valid & GW_VALID_SHADE) {
        pushed = set_u8(slot->shade_cur, view.shade_open_pct) || pushed;
        pushed = set_u8(slot->shade_tgt, view.shade_open_pct) || pushed;
    }
    if (view.valid & GW_VALID_BATTERY) {
        pushed = set_u8(slot->battery, view.battery_pct) || pushed;
    }
    if (view.valid & GW_VALID_LUX) {
        pushed = set_float(slot->lux, view.lux > 0.0001f ? view.lux : 0.0001f) || pushed;
    }
    if (view.valid & GW_VALID_LOCK) {
        pushed = set_u8(slot->lock_cur, view.lock_secured ? 1 : 0) || pushed;
        pushed = set_u8(slot->lock_tgt, view.lock_secured ? 1 : 0) || pushed;
    }
    if (view.valid & GW_VALID_THERMO) {
        pushed = set_u8(slot->thermo_mode, view.thermo_mode) || pushed;
        pushed = set_u8(slot->thermo_tgt, view.thermo_mode) || pushed;
        if (view.heat_set_c > 0) {
            slot->heat_set_c = view.heat_set_c;
            pushed = set_float(slot->thermo_set, view.heat_set_c) || pushed;
        }
    }
    if (view.valid & GW_VALID_SMOKE) {
        pushed = set_u8(slot->smoke, view.smoke ? 1 : 0) || pushed;
    }
    if (view.valid & GW_VALID_CO) {
        pushed = set_u8(slot->co, view.co_alarm ? 1 : 0) || pushed;
    }
    bool switch_changed = false;
    if (slot->kind == GW_KIND_BUTTON) {
        uint8_t count = view.button_count < slot->button_count ? view.button_count : slot->button_count;
        for (uint8_t i = 0; i < count; ++i) {
            if (view.button_press_n[i] == slot->seen_press_n[i]) {
                continue;
            }
            slot->seen_press_n[i] = view.button_press_n[i];
            if (slot->btn_mode[i] == 1) {
                pushed = set_bool_notify_edge(slot->btn_on[i], view.button_on[i] != 0) || pushed;
                switch_changed = true;
                matter_sniff_push(MATTER_SNIFF_EVT, view.node_id, 0x003b, view.button_ep[i], i,
                                  view.button_on[i] ? "homekit on" : "homekit off");
            } else if (slot->btn_event[i]) {
                uint8_t ev = view.button_ev[i] > 2 ? 0 : view.button_ev[i];
                hap_char_enable_notif_all_sessions(slot->btn_event[i]);
                hap_val_t single = {.i = static_cast<int>(ev)};
                const hap_val_t *cur = hap_char_get_val(slot->btn_event[i]);
                if (cur && cur->i == single.i) {
                    hap_char_raise_event(slot->btn_event[i]);
                } else {
                    hap_char_update_val(slot->btn_event[i], &single);
                }
                pushed = true;
                const char *label = ev == 2 ? "homekit long" : ev == 1 ? "homekit double" : "homekit press";
                matter_sniff_push(MATTER_SNIFF_EVT, view.node_id, 0x003b, view.button_ep[i], i, label);
            }
        }
    }
    /* The On value is already stored — that is why quitting Home shows it.
     * The live tile and automations move only when EVENT/1.0 reaches the
     * controller that is already connected. */
    (void)switch_changed;
    (void)pushed;
}

void sync_all()
{
    int count = gateway_nodes_count();
    for (int i = 0; i < count; ++i) {
        gateway_node_view_t view;
        if (gateway_nodes_copy(i, &view) == ESP_OK) {
            if (!find_slot(view.node_id)) {
                create_slot(view);
            }
        }
    }
}

void start_task(void *arg)
{
    (void)arg;
    /* Grab the internal block before the portal and sensor tasks allocate,
     * so pair-setup later has a stack that flash writes are allowed to use. */
    if (hap_platform_httpd_reserve_stack() != 0) {
        snprintf(s_status.status, sizeof(s_status.status), "no internal stack");
        ESP_LOGE(TAG, "HomeKit HTTP stack could not be reserved");
        vTaskDelete(NULL);
        return;
    }
    for (int i = 0; i < 80; ++i) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    hap_cfg_t cfg;
    hap_get_config(&cfg);
    cfg.task_priority = 3;
    cfg.task_stack_size = 12288;
    cfg.max_event_notif_chars = 48;
    cfg.disable_config_num_update = true;
    hap_set_config(&cfg);

    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        snprintf(s_status.status, sizeof(s_status.status), "hap_init failed");
        ESP_LOGE(TAG, "HomeKit init failed");
        hap_platform_httpd_release_stack();
        vTaskDelete(NULL);
        return;
    }

    hap_acc_cfg_t bridge_cfg = {
        .name = const_cast<char *>("ICC Thread Gateway"),
        .model = const_cast<char *>("ICC1-Thread-Bridge"),
        .manufacturer = const_cast<char *>("ICC"),
        .serial_num = const_cast<char *>("ICC1-THREAD"),
        .fw_rev = const_cast<char *>("0.3.0"),
        .hw_rev = const_cast<char *>("1.0"),
        .pv = const_cast<char *>("1.1.0"),
        .cid = HAP_CID_BRIDGE,
        .identify_routine = identify,
    };
    s_bridge = hap_acc_create(&bridge_cfg);
    if (!s_bridge) {
        snprintf(s_status.status, sizeof(s_status.status), "bridge create failed");
        hap_platform_httpd_release_stack();
        vTaskDelete(NULL);
        return;
    }
    uint8_t product[] = {'I', 'C', 'C', '1', 'T', 'H', 'R', 'D'};
    hap_acc_add_product_data(s_bridge, product, sizeof(product));
    hap_add_accessory(s_bridge);
    hap_set_setup_code(kSetupCode);
    hap_set_setup_id(kSetupId);

    lock();
    sync_all();
    unlock();

    int rc = HAP_FAIL;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        rc = hap_start();
        if (rc == HAP_SUCCESS) {
            break;
        }
        ESP_LOGW(TAG, "hap_start failed (try %d/5)", attempt);
        vTaskDelay(pdMS_TO_TICKS(1500 * attempt));
    }
    if (rc != HAP_SUCCESS) {
        snprintf(s_status.status, sizeof(s_status.status), "hap_start failed");
        ESP_LOGE(TAG, "HomeKit did not start; portal stays up");
        hap_platform_httpd_release_stack();
        vTaskDelete(NULL);
        return;
    }
    s_started = true;
    s_status.started = true;
    snprintf(s_status.status, sizeof(s_status.status), "%u accessory(ies)",
             static_cast<unsigned>(s_status.accessory_count));
    ESP_LOGI(TAG, "HomeKit bridge started — setup code %s  setup id %s  accessories=%u",
             kSetupCode, kSetupId, static_cast<unsigned>(s_status.accessory_count));
    vTaskDelete(NULL);
}

} // namespace

esp_err_t homekit_bridge_start(void)
{
    if (s_started || s_bridge) {
        return ESP_OK;
    }
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
    }
    snprintf(s_status.setup_code, sizeof(s_status.setup_code), "%s", kSetupCode);
    snprintf(s_status.setup_id, sizeof(s_status.setup_id), "%s", kSetupId);
    snprintf(s_status.status, sizeof(s_status.status), "starting");
    char code[] = "111-22-333";
    char ident[] = "THRD";
    char *payload = esp_hap_get_setup_payload(code, ident, false, HAP_CID_BRIDGE);
    if (payload) {
        snprintf(s_status.setup_uri, sizeof(s_status.setup_uri), "%s", payload);
        free(payload);
    }
    /* 6 KB leaves room for the 16 KB HomeKit HTTP stack and the 10 KB
     * hap-loop task, both created before this task exits. */
    if (xTaskCreate(start_task, "hk_start", 6144, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void homekit_bridge_attach(uint64_t node_id)
{
    if (!s_bridge) {
        return;
    }
    gateway_node_view_t view;
    if (gateway_nodes_copy_id(node_id, &view) != ESP_OK) {
        return;
    }
    lock();
    create_slot(view);
    unlock();
}

void homekit_bridge_on_node(uint64_t node_id)
{
    if (!s_started) {
        return;
    }
    gateway_node_view_t view;
    if (gateway_nodes_copy_id(node_id, &view) != ESP_OK) {
        return;
    }
    lock();
    slot_t *slot = find_slot(node_id);
    if (!slot) {
        create_slot(view);
        slot = find_slot(node_id);
    }
    bool rebuild = false;
    if (slot && view.kind == GW_KIND_BUTTON) {
        rebuild = slot->button_count != (view.button_count == 0 ? 1 : view.button_count);
        uint8_t count = view.button_count < slot->button_count ? view.button_count : slot->button_count;
        for (uint8_t i = 0; i < count && !rebuild; ++i) {
            const uint8_t mode = view.button_mode[i] == 1 ? 1 : 0;
            if (slot->btn_mode[i] != mode || slot->btn_ep[i] != view.button_ep[i] ||
                strcmp(slot->btn_label[i], view.button_name[i]) != 0) {
                rebuild = true;
            }
        }
    }
    unlock();
    if (rebuild) {
        homekit_bridge_detach(node_id);
        homekit_bridge_attach(node_id);
        return;
    }
    if (slot) {
        apply_view(slot, view);
    }
}

void homekit_bridge_rename(uint64_t node_id, const char *name)
{
    if (!name || !name[0] || !s_bridge) {
        return;
    }
    lock();
    slot_t *slot = find_slot(node_id);
    if (slot) {
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        if (slot->acc) {
            hap_serv_t *info =
                hap_acc_get_serv_by_uuid(slot->acc, HAP_SERV_UUID_ACCESSORY_INFORMATION);
            hap_char_t *hc = info ? hap_serv_get_char_by_uuid(info, HAP_CHAR_UUID_NAME) : nullptr;
            if (hc) {
                hap_val_t val = {};
                val.s = slot->name;
                hap_char_update_val_silent(hc, &val);
            }
        }
    }
    unlock();
}

void homekit_bridge_detach(uint64_t node_id)
{
    lock();
    slot_t *slot = find_slot(node_id);
    if (slot && slot->acc) {
        hap_remove_bridged_accessory(slot->acc);
        slot->acc = nullptr;
        if (s_status.accessory_count > 0) {
            s_status.accessory_count--;
        }
        if (s_started) {
            hap_update_config_number();
        }
    }
    if (slot) {
        memset(slot, 0, sizeof(*slot));
    }
    unlock();
}

void homekit_bridge_get_status(homekit_bridge_status_t *out)
{
    if (!out) {
        return;
    }
    *out = s_status;
    out->started = s_started;
    out->paired = s_started && hap_get_paired_controller_count() > 0;
}

esp_err_t homekit_bridge_reset_pairings(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (hap_get_paired_controller_count() == 0) {
        hap_pair_setup_re_enable();
        ESP_LOGI(TAG, "HomeKit pairing window reopened");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Clearing HomeKit pairings");
    if (hap_reset_pairings() != HAP_SUCCESS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
