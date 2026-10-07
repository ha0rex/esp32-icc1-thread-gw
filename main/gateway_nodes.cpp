#include "gateway_nodes.h"
#include "homekit_bridge.h"
#include "matter_sniff.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_matter_console.h"
#include "esp_matter_core.h"
#include "esp_matter_controller_cluster_command.h"
#include "esp_matter_controller_read_command.h"
#include "esp_matter_controller_subscribe_command.h"
#include "esp_matter_controller_client.h"
#include "esp_matter_controller_utils.h"
#include "esp_matter_controller_write_command.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lib/core/ScopedNodeId.h"
#include "lib/core/TLV.h"
#include "lib/support/CHIPMem.h"
#include "nvs.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"
#include "openthread/thread_ftd.h"
#include "platform/PlatformManager.h"

#include <controller/CHIPDeviceController.h>
#include <inet/IPAddress.h>
#include <transport/SecureSession.h>

namespace {

constexpr char kNvsNamespace[] = "gw_nodes";
constexpr char kNvsKey[] = "registry";
constexpr size_t kMaxNodes = 8;
constexpr uint32_t kRegistryVersion = 1;

constexpr uint32_t kDescriptorCluster = 0x001d;
constexpr uint32_t kBooleanStateCluster = 0x0045;
constexpr uint32_t kOccupancyCluster = 0x0406;
constexpr uint32_t kStateValueAttribute = 0x0000;
constexpr uint32_t kOccupancyAttribute = 0x0000;
constexpr uint16_t kWildcardEndpoint = 0xffff;
constexpr uint32_t kWildcardAttribute = 0xffffffff;

enum class sensor_kind_t : uint8_t {
    contact = GW_KIND_CONTACT,
    motion = GW_KIND_MOTION,
    leak = GW_KIND_LEAK,
    climate = GW_KIND_CLIMATE,
    air = GW_KIND_AIR,
    plug = GW_KIND_PLUG,
    light = GW_KIND_LIGHT,
    light_ct = GW_KIND_LIGHT_CT,
    light_color = GW_KIND_LIGHT_COLOR,
    button = GW_KIND_BUTTON,
    shade = GW_KIND_SHADE,
    onoff = GW_KIND_ONOFF,
    occupancy = GW_KIND_OCCUPANCY,
    lux = GW_KIND_LUX,
    lock = GW_KIND_LOCK,
    thermostat = GW_KIND_THERMOSTAT,
    fan = GW_KIND_FAN,
    smoke = GW_KIND_SMOKE,
    wall = GW_KIND_SWITCH,
};

struct live_t {
    uint32_t valid;
    bool on;
    bool contact_open;
    bool motion;
    bool leak;
    uint8_t level;
    uint8_t hue;
    uint8_t sat;
    uint16_t mireds;
    int16_t temp_x100;
    uint16_t hum_x100;
    float co2;
    float pm25;
    int32_t power_mw;
    uint8_t shade_closed_pct;
    uint8_t battery_pct;
    uint8_t button_pos;
    uint32_t button_presses;
    uint8_t button_event;
    float lux;
    bool lock_secured;
    uint8_t thermo_mode;
    int16_t heat_set_x100;
    bool smoke;
    bool co_alarm;
};

struct sub_t {
    uint32_t cluster;
    uint32_t attribute;
};

struct node_entry_t {
    uint64_t node_id;
    uint16_t endpoint_id;
    sensor_kind_t kind;
    uint8_t reserved;
    char name[24];
};

struct node_registry_t {
    uint32_t version;
    uint8_t count;
    uint8_t reserved[3];
    node_entry_t entries[kMaxNodes];
};

const char *TAG = "gateway_nodes";
node_registry_t s_registry = {.version = kRegistryVersion};
char s_state[kMaxNodes][64];
live_t s_live[kMaxNodes];
SemaphoreHandle_t s_lock;
bool s_loaded;
enum class watch_state_t : uint8_t { none = 0, inflight, up, down };

struct watch_t {
    uint64_t node_id;
    void *cmd;
    int64_t since_us;
    watch_state_t state;
};

watch_t s_watch[kMaxNodes];
int64_t s_last_watch_us;

void lock_registry()
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void unlock_registry()
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

watch_t *find_watch_locked(uint64_t node_id)
{
    for (watch_t &slot : s_watch) {
        if (slot.node_id == node_id) {
            return &slot;
        }
    }
    return nullptr;
}

watch_t *take_watch_locked(uint64_t node_id)
{
    watch_t *slot = find_watch_locked(node_id);
    if (slot) {
        return slot;
    }
    for (watch_t &entry : s_watch) {
        if (entry.node_id == 0) {
            entry.node_id = node_id;
            return &entry;
        }
    }
    return nullptr;
}

void watch_begin(uint64_t node_id, void *cmd)
{
    lock_registry();
    watch_t *slot = take_watch_locked(node_id);
    if (slot) {
        slot->cmd = cmd;
        slot->state = watch_state_t::inflight;
        slot->since_us = esp_timer_get_time();
        s_last_watch_us = slot->since_us;
    }
    unlock_registry();
}

uint64_t watch_fail(void *cmd)
{
    lock_registry();
    uint64_t node_id = 0;
    for (watch_t &slot : s_watch) {
        if (slot.cmd == cmd && cmd) {
            node_id = slot.node_id;
            slot.cmd = nullptr;
            slot.state = watch_state_t::down;
            slot.since_us = esp_timer_get_time();
        }
    }
    unlock_registry();
    return node_id;
}

void watch_down(uint64_t node_id)
{
    lock_registry();
    watch_t *slot = find_watch_locked(node_id);
    if (slot) {
        slot->cmd = nullptr;
        slot->state = watch_state_t::down;
        slot->since_us = esp_timer_get_time();
    }
    unlock_registry();
}

void watch_up(uint64_t node_id)
{
    lock_registry();
    watch_t *slot = find_watch_locked(node_id);
    if (slot) {
        slot->state = watch_state_t::up;
    }
    unlock_registry();
}

void watch_forget(uint64_t node_id)
{
    lock_registry();
    watch_t *slot = find_watch_locked(node_id);
    if (slot) {
        memset(slot, 0, sizeof(*slot));
    }
    unlock_registry();
}

bool watch_is_inflight(uint64_t node_id)
{
    lock_registry();
    watch_t *slot = find_watch_locked(node_id);
    bool inflight = slot && slot->state == watch_state_t::inflight;
    unlock_registry();
    return inflight;
}

bool watch_is_up(uint64_t node_id)
{
    lock_registry();
    watch_t *slot = find_watch_locked(node_id);
    bool up = slot && slot->state == watch_state_t::up;
    unlock_registry();
    return up;
}

constexpr size_t kMaxButtons = 6;

struct btn_t {
    uint16_t endpoint;
    uint8_t position;
    uint8_t mode;
    uint8_t on;
    uint8_t last_pos;
    char name[24];
    uint32_t presses;
    uint8_t event;
    int64_t last_us;
};

struct remote_t {
    uint64_t node_id;
    uint8_t count;
    btn_t buttons[kMaxButtons];
};

struct btn_nvs_t {
    uint16_t endpoint;
    uint8_t position;
    uint8_t mode;
    uint8_t on;
    uint8_t reserved;
    char name[24];
};

struct remote_nvs_t {
    uint64_t node_id;
    uint8_t count;
    uint8_t reserved[3];
    btn_nvs_t buttons[kMaxButtons];
};

struct remote_store_t {
    uint32_t version;
    remote_nvs_t remotes[kMaxNodes];
};

constexpr uint32_t kRemoteVersion = 1;
constexpr char kRemoteKey[] = "buttons";

remote_t s_remotes[kMaxNodes];

remote_t *find_remote_locked(uint64_t node_id)
{
    for (remote_t &remote : s_remotes) {
        if (remote.node_id == node_id) {
            return &remote;
        }
    }
    return nullptr;
}

remote_t *take_remote_locked(uint64_t node_id)
{
    remote_t *remote = find_remote_locked(node_id);
    if (remote) {
        return remote;
    }
    for (remote_t &slot : s_remotes) {
        if (slot.node_id == 0) {
            memset(&slot, 0, sizeof(slot));
            slot.node_id = node_id;
            return &slot;
        }
    }
    return nullptr;
}

void save_remotes()
{
    remote_store_t store = {};
    store.version = kRemoteVersion;
    lock_registry();
    for (size_t i = 0; i < kMaxNodes; ++i) {
        store.remotes[i].node_id = s_remotes[i].node_id;
        store.remotes[i].count = s_remotes[i].count;
        for (uint8_t b = 0; b < s_remotes[i].count && b < kMaxButtons; ++b) {
            store.remotes[i].buttons[b].endpoint = s_remotes[i].buttons[b].endpoint;
            store.remotes[i].buttons[b].position = s_remotes[i].buttons[b].position;
            store.remotes[i].buttons[b].mode = s_remotes[i].buttons[b].mode;
            store.remotes[i].buttons[b].on = s_remotes[i].buttons[b].on;
            snprintf(store.remotes[i].buttons[b].name, sizeof(store.remotes[i].buttons[b].name), "%s",
                     s_remotes[i].buttons[b].name);
        }
    }
    unlock_registry();
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, kRemoteKey, &store, sizeof(store));
    nvs_commit(handle);
    nvs_close(handle);
}

void load_remotes()
{
    memset(s_remotes, 0, sizeof(s_remotes));
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    remote_store_t store = {};
    size_t size = sizeof(store);
    esp_err_t err = nvs_get_blob(handle, kRemoteKey, &store, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(store) || store.version != kRemoteVersion) {
        return;
    }
    for (size_t i = 0; i < kMaxNodes; ++i) {
        if (store.remotes[i].node_id == 0 || store.remotes[i].count > kMaxButtons) {
            continue;
        }
        s_remotes[i].node_id = store.remotes[i].node_id;
        s_remotes[i].count = store.remotes[i].count;
        for (uint8_t b = 0; b < store.remotes[i].count; ++b) {
            s_remotes[i].buttons[b].endpoint = store.remotes[i].buttons[b].endpoint;
            s_remotes[i].buttons[b].position = store.remotes[i].buttons[b].position;
            s_remotes[i].buttons[b].mode = store.remotes[i].buttons[b].mode ? 1 : 0;
            s_remotes[i].buttons[b].on = store.remotes[i].buttons[b].on ? 1 : 0;
            snprintf(s_remotes[i].buttons[b].name, sizeof(s_remotes[i].buttons[b].name), "%s",
                     store.remotes[i].buttons[b].name);
        }
    }
}

void drop_remote_locked(uint64_t node_id)
{
    remote_t *remote = find_remote_locked(node_id);
    if (remote) {
        memset(remote, 0, sizeof(*remote));
    }
}

bool button_less(const btn_t &button, uint16_t endpoint, uint8_t position)
{
    if (button.endpoint != endpoint) {
        return button.endpoint > endpoint;
    }
    return button.position > position;
}

bool ensure_button_locked(uint64_t node_id, uint16_t endpoint, uint8_t position, int &index)
{
    if (endpoint == 0) {
        index = -1;
        return false;
    }
    remote_t *remote = take_remote_locked(node_id);
    if (!remote) {
        index = -1;
        return false;
    }
    const uint8_t pos = position > 1 ? position : 0;
    for (uint8_t i = 0; i < remote->count; ++i) {
        if (remote->buttons[i].endpoint == endpoint && remote->buttons[i].position == pos) {
            index = i;
            return false;
        }
    }
    if (remote->count >= kMaxButtons) {
        index = 0;
        return false;
    }
    uint8_t at = remote->count;
    for (uint8_t i = 0; i < remote->count; ++i) {
        if (button_less(remote->buttons[i], endpoint, pos)) {
            at = i;
            break;
        }
    }
    memmove(&remote->buttons[at + 1], &remote->buttons[at],
            (remote->count - at) * sizeof(btn_t));
    memset(&remote->buttons[at], 0, sizeof(btn_t));
    remote->buttons[at].endpoint = endpoint;
    remote->buttons[at].position = pos;
    snprintf(remote->buttons[at].name, sizeof(remote->buttons[at].name), "Button %u",
             static_cast<unsigned>(at + 1));
    remote->count++;
    index = at;
    return true;
}

const char *kind_name(sensor_kind_t kind)
{
    switch (kind) {
    case sensor_kind_t::contact:
        return "contact";
    case sensor_kind_t::motion:
        return "motion";
    case sensor_kind_t::leak:
        return "leak";
    case sensor_kind_t::climate:
        return "climate";
    case sensor_kind_t::air:
        return "air";
    case sensor_kind_t::plug:
        return "plug";
    case sensor_kind_t::light:
        return "light";
    case sensor_kind_t::light_ct:
        return "light_ct";
    case sensor_kind_t::light_color:
        return "light_color";
    case sensor_kind_t::button:
        return "button";
    case sensor_kind_t::shade:
        return "shade";
    case sensor_kind_t::onoff:
        return "onoff";
    case sensor_kind_t::occupancy:
        return "occupancy";
    case sensor_kind_t::lux:
        return "illuminance";
    case sensor_kind_t::lock:
        return "lock";
    case sensor_kind_t::thermostat:
        return "thermostat";
    case sensor_kind_t::fan:
        return "fan";
    case sensor_kind_t::smoke:
        return "smoke";
    case sensor_kind_t::wall:
        return "switch";
    default:
        return "unknown";
    }
}

bool parse_kind(const char *value, sensor_kind_t &kind)
{
    if (!value) {
        return false;
    }
    if (strcmp(value, "contact") == 0 || strcmp(value, "window") == 0) {
        kind = sensor_kind_t::contact;
    } else if (strcmp(value, "motion") == 0) {
        kind = sensor_kind_t::motion;
    } else if (strcmp(value, "leak") == 0 || strcmp(value, "water") == 0) {
        kind = sensor_kind_t::leak;
    } else if (strcmp(value, "climate") == 0 || strcmp(value, "temperature") == 0 ||
               strcmp(value, "humidity") == 0) {
        kind = sensor_kind_t::climate;
    } else if (strcmp(value, "air") == 0 || strcmp(value, "airquality") == 0) {
        kind = sensor_kind_t::air;
    } else if (strcmp(value, "plug") == 0 || strcmp(value, "outlet") == 0) {
        kind = sensor_kind_t::plug;
    } else if (strcmp(value, "light") == 0 || strcmp(value, "dimmer") == 0) {
        kind = sensor_kind_t::light;
    } else if (strcmp(value, "light_ct") == 0 || strcmp(value, "colortemp") == 0) {
        kind = sensor_kind_t::light_ct;
    } else if (strcmp(value, "light_color") == 0 || strcmp(value, "color") == 0) {
        kind = sensor_kind_t::light_color;
    } else if (strcmp(value, "button") == 0 || strcmp(value, "remote") == 0) {
        kind = sensor_kind_t::button;
    } else if (strcmp(value, "shade") == 0 || strcmp(value, "blind") == 0 ||
               strcmp(value, "covering") == 0) {
        kind = sensor_kind_t::shade;
    } else if (strcmp(value, "onoff") == 0 || strcmp(value, "on_off") == 0) {
        kind = sensor_kind_t::onoff;
    } else if (strcmp(value, "occupancy") == 0) {
        kind = sensor_kind_t::occupancy;
    } else if (strcmp(value, "illuminance") == 0 || strcmp(value, "lux") == 0 ||
               strcmp(value, "lightsensor") == 0) {
        kind = sensor_kind_t::lux;
    } else if (strcmp(value, "lock") == 0 || strcmp(value, "doorlock") == 0) {
        kind = sensor_kind_t::lock;
    } else if (strcmp(value, "thermostat") == 0 || strcmp(value, "thermo") == 0) {
        kind = sensor_kind_t::thermostat;
    } else if (strcmp(value, "fan") == 0) {
        kind = sensor_kind_t::fan;
    } else if (strcmp(value, "smoke") == 0 || strcmp(value, "co") == 0) {
        kind = sensor_kind_t::smoke;
    } else if (strcmp(value, "switch") == 0) {
        kind = sensor_kind_t::wall;
    } else {
        return false;
    }
    return true;
}

esp_err_t save_registry()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, kNvsKey, &s_registry, sizeof(s_registry));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

constexpr uint32_t kClusterOnOff = 0x0006;
constexpr uint32_t kClusterLevel = 0x0008;
constexpr uint32_t kClusterColor = 0x0300;
constexpr uint32_t kClusterBoolean = 0x0045;
constexpr uint32_t kClusterOccupancy = 0x0406;
constexpr uint32_t kClusterTemp = 0x0402;
constexpr uint32_t kClusterHumidity = 0x0405;
constexpr uint32_t kClusterCo2 = 0x040d;
constexpr uint32_t kClusterPm25 = 0x042a;
constexpr uint32_t kClusterPower = 0x0090;
constexpr uint32_t kClusterPowerSource = 0x002f;
constexpr uint32_t kClusterSwitch = 0x003b;
constexpr uint32_t kClusterCover = 0x0102;
constexpr uint32_t kClusterIllum = 0x0400;
constexpr uint32_t kClusterLock = 0x0101;
constexpr uint32_t kClusterThermo = 0x0201;
constexpr uint32_t kClusterFan = 0x0202;
constexpr uint32_t kClusterSmoke = 0x005c;

constexpr uint32_t kAttrHeatSet = 0x0012;
constexpr uint32_t kAttrCoolSet = 0x0011;
constexpr uint32_t kAttrSystemMode = 0x001c;
constexpr uint32_t kAttrSmokeState = 0x0001;
constexpr uint32_t kAttrCoState = 0x0002;

constexpr uint32_t kAttrValue = 0x0000;
constexpr uint32_t kAttrSat = 0x0001;
constexpr uint32_t kAttrMireds = 0x0007;
constexpr uint32_t kAttrActivePower = 0x0008;
constexpr uint32_t kAttrSwitchPos = 0x0001;
constexpr uint32_t kAttrLiftPct = 0x0008;
constexpr uint32_t kAttrBattery = 0x000c;

bool read_bool(chip::TLV::TLVReader *data, bool &out)
{
    if (!data) {
        return false;
    }
    chip::TLV::TLVReader reader = *data;
    if (reader.Get(out) == CHIP_NO_ERROR) {
        return true;
    }
    uint64_t number = 0;
    reader = *data;
    if (reader.Get(number) == CHIP_NO_ERROR) {
        out = number != 0;
        return true;
    }
    reader = *data;
    if (reader.Next() != CHIP_NO_ERROR) {
        return false;
    }
    if (reader.Get(out) == CHIP_NO_ERROR) {
        return true;
    }
    if (reader.Get(number) == CHIP_NO_ERROR) {
        out = number != 0;
        return true;
    }
    return false;
}

bool read_u64(chip::TLV::TLVReader *data, uint64_t &out)
{
    if (!data) {
        return false;
    }
    chip::TLV::TLVReader reader = *data;
    if (reader.Get(out) == CHIP_NO_ERROR) {
        return true;
    }
    reader = *data;
    return reader.Next() == CHIP_NO_ERROR && reader.Get(out) == CHIP_NO_ERROR;
}

bool read_motion_value(chip::TLV::TLVReader *data, bool &active)
{
    if (read_bool(data, active)) {
        return true;
    }
    uint64_t number = 0;
    if (read_u64(data, number)) {
        active = (number & 0x01) != 0;
        return true;
    }
    if (!data) {
        return false;
    }
    chip::TLV::TLVReader reader = *data;
    if (reader.GetType() != chip::TLV::kTLVType_Structure && reader.Next() != CHIP_NO_ERROR) {
        return false;
    }
    if (reader.GetType() != chip::TLV::kTLVType_Structure) {
        reader = *data;
        if (reader.Next() != CHIP_NO_ERROR || reader.GetType() != chip::TLV::kTLVType_Structure) {
            return false;
        }
    }
    chip::TLV::TLVType outer;
    if (reader.EnterContainer(outer) != CHIP_NO_ERROR) {
        return false;
    }
    while (reader.Next() == CHIP_NO_ERROR) {
        if (!chip::TLV::IsContextTag(reader.GetTag()) || chip::TLV::TagNumFromTag(reader.GetTag()) != 0) {
            continue;
        }
        if (reader.Get(active) == CHIP_NO_ERROR) {
            return true;
        }
        if (reader.Get(number) == CHIP_NO_ERROR) {
            active = (number & 0x01) != 0;
            return true;
        }
    }
    return false;
}

bool read_float(chip::TLV::TLVReader *data, float &out)
{
    if (!data) {
        return false;
    }
    chip::TLV::TLVReader copy = *data;
    if (copy.Get(out) == CHIP_NO_ERROR) {
        return true;
    }
    double wide = 0;
    if (data->Get(wide) == CHIP_NO_ERROR) {
        out = static_cast<float>(wide);
        return true;
    }
    return false;
}

void format_summary(const node_entry_t &entry, const live_t &live, char *out, size_t out_len)
{
    switch (entry.kind) {
    case sensor_kind_t::contact:
        if ((live.valid & GW_VALID_CONTACT) && (live.valid & GW_VALID_MOTION)) {
            snprintf(out, out_len, "%s · %s", live.contact_open ? "Open" : "Closed",
                     live.motion ? "Detected" : "Clear");
        } else if (live.valid & GW_VALID_CONTACT) {
            snprintf(out, out_len, "%s", live.contact_open ? "Open" : "Closed");
        } else if (live.valid & GW_VALID_MOTION) {
            snprintf(out, out_len, "%s", live.motion ? "Detected" : "Clear");
        } else {
            snprintf(out, out_len, "waiting");
        }
        break;
    case sensor_kind_t::motion:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_MOTION)
                     ? (live.motion ? "DETECTED" : "CLEAR")
                     : "waiting");
        break;
    case sensor_kind_t::leak:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_LEAK) ? (live.leak ? "LEAK" : "DRY")
                                              : "waiting");
        break;
    case sensor_kind_t::climate:
        if (live.valid & GW_VALID_TEMP) {
            snprintf(out, out_len, "%.1fC %.0f%%", live.temp_x100 / 100.0,
                     (live.valid & GW_VALID_HUMIDITY) ? live.hum_x100 / 100.0 : 0);
        } else {
            snprintf(out, out_len, "waiting");
        }
        break;
    case sensor_kind_t::air:
        snprintf(out, out_len, "CO2 %.0f  PM2.5 %.0f  %.1fC",
                 (live.valid & GW_VALID_CO2) ? live.co2 : 0,
                 (live.valid & GW_VALID_PM25) ? live.pm25 : 0,
                 (live.valid & GW_VALID_TEMP) ? live.temp_x100 / 100.0 : 0);
        break;
    case sensor_kind_t::plug:
        snprintf(out, out_len, "%s  %.1f W",
                 (live.valid & GW_VALID_ON) ? (live.on ? "ON" : "OFF") : "waiting",
                 (live.valid & GW_VALID_POWER) ? live.power_mw / 1000.0 : 0);
        break;
    case sensor_kind_t::light:
    case sensor_kind_t::light_ct:
    case sensor_kind_t::light_color:
        snprintf(out, out_len, "%s  %u%%",
                 (live.valid & GW_VALID_ON) ? (live.on ? "ON" : "OFF") : "waiting",
                 (live.valid & GW_VALID_LEVEL) ? (live.level * 100 / 254) : 0);
        break;
    case sensor_kind_t::button: {
        const remote_t *remote = find_remote_locked(entry.node_id);
        if (remote && remote->count > 1) {
            char piece[64];
            size_t used = 0;
            out[0] = 0;
            for (uint8_t i = 0; i < remote->count && used + 1 < out_len; ++i) {
                const btn_t &button = remote->buttons[i];
                if (button.mode == 1) {
                    snprintf(piece, sizeof(piece), "%s%s %s", i ? " · " : "",
                             button.name[0] ? button.name : "Button", button.on ? "on" : "off");
                } else {
                    snprintf(piece, sizeof(piece), "%s%s", i ? " · " : "",
                             button.name[0] ? button.name : "Button");
                }
                size_t n = strlen(piece);
                if (used + n >= out_len) {
                    break;
                }
                memcpy(out + used, piece, n + 1);
                used += n;
            }
        } else if (remote && remote->count == 1 && remote->buttons[0].mode == 1) {
            snprintf(out, out_len, "%s", remote->buttons[0].on ? "On" : "Off");
        } else {
            snprintf(out, out_len, "%lu presses",
                     static_cast<unsigned long>(live.button_presses));
        }
        break;
    }
    case sensor_kind_t::shade:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_SHADE)
                     ? "open"
                     : "waiting");
        if (live.valid & GW_VALID_SHADE) {
            snprintf(out, out_len, "%u%% open",
                     100 - live.shade_closed_pct);
        }
        break;
    case sensor_kind_t::onoff:
    case sensor_kind_t::wall:
    case sensor_kind_t::fan:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_ON) ? (live.on ? "ON" : "OFF") : "waiting");
        break;
    case sensor_kind_t::occupancy:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_MOTION)
                     ? (live.motion ? "OCCUPIED" : "CLEAR")
                     : "waiting");
        break;
    case sensor_kind_t::lux:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_LUX) ? "" : "waiting");
        if (live.valid & GW_VALID_LUX) {
            snprintf(out, out_len, "%.0f lux", live.lux);
        }
        break;
    case sensor_kind_t::lock:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_LOCK)
                     ? (live.lock_secured ? "LOCKED" : "UNLOCKED")
                     : "waiting");
        break;
    case sensor_kind_t::thermostat:
        if (live.valid & GW_VALID_TEMP) {
            snprintf(out, out_len, "%.1fC set %.1fC", live.temp_x100 / 100.0,
                     live.heat_set_x100 / 100.0);
        } else {
            snprintf(out, out_len, "waiting");
        }
        break;
    case sensor_kind_t::smoke:
        snprintf(out, out_len, "%s",
                 (live.valid & GW_VALID_SMOKE)
                     ? (live.smoke ? "SMOKE" : (live.co_alarm ? "CO" : "CLEAR"))
                     : "waiting");
        break;
    default:
        snprintf(out, out_len, "waiting");
        break;
    }
}

node_entry_t *find_node(uint64_t node_id);

void fill_view(const node_entry_t &entry, const live_t &live, gateway_node_view_t *out)
{
    memset(out, 0, sizeof(*out));
    out->node_id = entry.node_id;
    out->endpoint_id = entry.endpoint_id;
    out->kind = static_cast<gateway_kind_t>(entry.kind);
    snprintf(out->name, sizeof(out->name), "%s", entry.name);
    format_summary(entry, live, out->state, sizeof(out->state));
    out->valid = live.valid;
    out->on = live.on;
    out->level_pct = static_cast<uint8_t>(live.level * 100 / 254);
    out->mireds = live.mireds;
    out->hue_deg = live.hue * 360.0f / 254.0f;
    out->saturation_pct = live.sat * 100.0f / 254.0f;
    out->contact_open = live.contact_open;
    out->motion = live.motion;
    out->leak = live.leak;
    out->temp_c = live.temp_x100 / 100.0f;
    out->humidity_pct = live.hum_x100 / 100.0f;
    out->co2_ppm = live.co2;
    out->pm25 = live.pm25;
    out->power_w = live.power_mw / 1000.0f;
    out->shade_open_pct = static_cast<uint8_t>(100 - live.shade_closed_pct);
    out->battery_pct = live.battery_pct;
    out->button_presses = live.button_presses;
    out->button_event = live.button_event;
    if (const remote_t *remote = find_remote_locked(entry.node_id)) {
        out->button_count = remote->count;
        for (uint8_t i = 0; i < remote->count && i < 6; ++i) {
            out->button_mode[i] = remote->buttons[i].mode;
            out->button_on[i] = remote->buttons[i].on;
            out->button_ep[i] = remote->buttons[i].endpoint;
            out->button_press_n[i] = remote->buttons[i].presses;
            out->button_ev[i] = remote->buttons[i].event;
            snprintf(out->button_name[i], sizeof(out->button_name[i]), "%s", remote->buttons[i].name);
        }
    }
    out->lux = live.lux;
    out->lock_secured = live.lock_secured;
    out->thermo_mode = live.thermo_mode;
    out->heat_set_c = live.heat_set_x100 / 100.0f;
    out->smoke = live.smoke;
    out->co_alarm = live.co_alarm;
}

void apply_active(node_entry_t *entry, live_t &live, bool active, bool occupancy, bool &changed)
{
    if (occupancy || entry->kind == sensor_kind_t::motion || entry->kind == sensor_kind_t::occupancy) {
        live.motion = active;
        live.valid |= GW_VALID_MOTION;
    } else if (entry->kind == sensor_kind_t::leak) {
        live.leak = active;
        live.valid |= GW_VALID_LEAK;
    } else {
        // Matter StateValue true means the magnet is present, so the window is closed.
        bool open = !active;
        live.contact_open = open;
        live.valid |= GW_VALID_CONTACT;
        live.motion = open;
        live.valid |= GW_VALID_MOTION;
    }
    changed = true;
}

void sniff_report(uint8_t dir, uint64_t node, uint32_t cluster, uint16_t endpoint, uint32_t id,
                  chip::TLV::TLVReader *data, const char *what)
{
    char text[40];
    if (!data) {
        snprintf(text, sizeof(text), "%s", what ? what : "");
    } else {
        chip::TLV::TLVReader reader = *data;
        bool flag = false;
        uint64_t number = 0;
        if (reader.Get(flag) == CHIP_NO_ERROR) {
            snprintf(text, sizeof(text), "%s %s", what, flag ? "true" : "false");
        } else if (reader.Get(number) == CHIP_NO_ERROR) {
            snprintf(text, sizeof(text), "%s %llu", what, static_cast<unsigned long long>(number));
        } else {
            snprintf(text, sizeof(text), "%s type %u", what, static_cast<unsigned>(reader.GetType()));
        }
    }
    matter_sniff_push(dir, node, cluster, endpoint, id, text);
}

void on_subscribe_done(uint64_t node_id, uint32_t subscription_id)
{
    char text[40];
    snprintf(text, sizeof(text), "subscription ended %lu", static_cast<unsigned long>(subscription_id));
    matter_sniff_push(MATTER_SNIFF_TX, node_id, 0, 0, 0, text);
    watch_down(node_id);
}

void on_subscribe_established(uint64_t node_id, uint32_t subscription_id)
{
    char text[48];
    snprintf(text, sizeof(text), "listening %lu", static_cast<unsigned long>(subscription_id));
    matter_sniff_push(MATTER_SNIFF_TX, node_id, 0, 0, 0, text);
    watch_up(node_id);
    ESP_LOGI(TAG, "Listening for reports from node %" PRIu64, node_id);
}

void on_subscribe_fail(void *cmd)
{
    uint64_t node_id = watch_fail(cmd);
    matter_sniff_push(MATTER_SNIFF_TX, node_id, 0, 0, 0, "subscribe failed");
}

uint8_t read_switch_position(chip::TLV::TLVReader *data)
{
    if (!data) {
        return 0;
    }
    chip::TLV::TLVReader reader = *data;
    uint64_t number = 0;
    if (reader.Get(number) == CHIP_NO_ERROR) {
        return static_cast<uint8_t>(number > 255 ? 255 : number);
    }
    reader = *data;
    if (reader.GetType() != chip::TLV::kTLVType_Structure && reader.Next() != CHIP_NO_ERROR) {
        return 0;
    }
    if (reader.GetType() != chip::TLV::kTLVType_Structure) {
        reader = *data;
        if (reader.Next() != CHIP_NO_ERROR || reader.GetType() != chip::TLV::kTLVType_Structure) {
            return 0;
        }
    }
    chip::TLV::TLVType outer;
    if (reader.EnterContainer(outer) != CHIP_NO_ERROR) {
        return 0;
    }
    while (reader.Next() == CHIP_NO_ERROR) {
        if (!chip::TLV::IsContextTag(reader.GetTag()) || chip::TLV::TagNumFromTag(reader.GetTag()) != 0) {
            continue;
        }
        if (reader.Get(number) == CHIP_NO_ERROR) {
            return static_cast<uint8_t>(number > 255 ? 255 : number);
        }
    }
    return 0;
}

void note_remote_press(remote_t &remote, int index, live_t &live, uint32_t matter_event, uint8_t hap_event,
                       bool &changed)
{
    if (index < 0 || static_cast<size_t>(index) >= remote.count) {
        return;
    }
    btn_t &button = remote.buttons[index];
    int64_t now = esp_timer_get_time();
    if (button.mode == 1) {
        // One click is InitialPress, a CurrentPosition blip, ShortRelease, then
        // MultiPressComplete about half a second later. Toggling on each of
        // those puts the switch back where it started, so Home never sees a
        // lasting change. Only the press itself (or a position edge when the
        // press event was missed) changes the latch.
        const bool opening = matter_event == 0x00 || matter_event == 0x01 || matter_event == 0xffffffff;
        if (!opening || now - button.last_us < 250000) {
            if (hap_event > button.event) {
                button.event = hap_event;
                live.button_event = button.event;
            }
            return;
        }
    } else if (now - button.last_us < 300000) {
        if (hap_event > button.event) {
            button.event = hap_event;
            live.button_event = button.event;
        }
        return;
    }
    button.last_us = now;
    button.presses++;
    button.event = hap_event > 2 ? 0 : hap_event;
    if (button.mode == 1) {
        button.on = button.on ? 0 : 1;
    }
    live.button_presses++;
    live.button_event = button.event;
    changed = true;
}

void sensor_event_report(uint64_t node_id, const chip::app::EventHeader &header, chip::TLV::TLVReader *data)
{
    watch_up(node_id);
    sniff_report(MATTER_SNIFF_EVT, node_id, header.mPath.mClusterId, header.mPath.mEndpointId,
                 header.mPath.mEventId, data, "event");
    if (header.mPath.mClusterId == kClusterSwitch) {
        uint8_t hap_event = 0;
        const uint32_t id = header.mPath.mEventId;
        if (id == 0x02 || id == 0x04) {
            hap_event = 2;
        } else if (id == 0x06) {
            hap_event = 1;
        }
        const uint8_t position = read_switch_position(data);
        lock_registry();
        node_entry_t *entry = find_node(node_id);
        if (!entry || entry->kind != sensor_kind_t::button) {
            unlock_registry();
            return;
        }
        size_t index = static_cast<size_t>(entry - s_registry.entries);
        int button = -1;
        bool grew = ensure_button_locked(node_id, header.mPath.mEndpointId, position, button);
        bool changed = false;
        remote_t *remote = find_remote_locked(node_id);
        if (remote) {
            note_remote_press(*remote, button, s_live[index], id, hap_event, changed);
        }
        if (changed || grew) {
            format_summary(*entry, s_live[index], s_state[index], sizeof(s_state[index]));
            ESP_LOGI(TAG, "button node=%" PRIu64 " ep=%u %s", node_id, header.mPath.mEndpointId,
                     s_state[index]);
        }
        unlock_registry();
        if (grew) {
            save_remotes();
        }
        if (changed || grew) {
            homekit_bridge_on_node(node_id);
        }
        return;
    }
    const bool occupancy = header.mPath.mClusterId == kClusterOccupancy;
    const bool boolean = header.mPath.mClusterId == kClusterBoolean;
    if (!occupancy && !boolean) {
        return;
    }
    bool active = false;
    if (!read_motion_value(data, active)) {
        ESP_LOGW(TAG, "Sensor event from node %" PRIu64 " did not decode", node_id);
        return;
    }
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return;
    }
    size_t index = static_cast<size_t>(entry - s_registry.entries);
    bool changed = false;
    apply_active(entry, s_live[index], active, occupancy, changed);
    if (changed) {
        format_summary(*entry, s_live[index], s_state[index], sizeof(s_state[index]));
        ESP_LOGI(TAG, "%s node=%" PRIu64 " %s", kind_name(entry->kind), node_id, s_state[index]);
    }
    unlock_registry();
    if (changed) {
        homekit_bridge_on_node(node_id);
    }
}

void sensor_attribute_report(uint64_t node_id,
                             const chip::app::ConcreteDataAttributePath &path,
                             chip::TLV::TLVReader *data)
{
    watch_up(node_id);
    sniff_report(MATTER_SNIFF_RX, node_id, path.mClusterId, path.mEndpointId, path.mAttributeId, data,
                 "attr");
    if (!data) {
        return;
    }
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return;
    }
    size_t index = static_cast<size_t>(entry - s_registry.entries);
    live_t &live = s_live[index];
    live_t before = live;
    bool changed = false;
    bool layout = false;

    if (path.mClusterId == kClusterBoolean && path.mAttributeId == kAttrValue) {
        bool value = false;
        if (read_motion_value(data, value)) {
            if (entry->kind == sensor_kind_t::leak) {
                live.leak = value;
                live.valid |= GW_VALID_LEAK;
            } else if (entry->kind == sensor_kind_t::motion ||
                       entry->kind == sensor_kind_t::occupancy) {
                live.motion = value;
                live.valid |= GW_VALID_MOTION;
            } else {
                // Matter StateValue true means the magnet is present, so the window is closed.
                bool open = !value;
                live.contact_open = open;
                live.valid |= GW_VALID_CONTACT;
                live.motion = open;
                live.valid |= GW_VALID_MOTION;
            }
            changed = true;
        } else {
            ESP_LOGW(TAG, "Boolean report from node %" PRIu64 " did not decode", node_id);
        }
    } else if (path.mClusterId == kClusterOccupancy && path.mAttributeId == kAttrValue) {
        bool occupied = false;
        if (read_motion_value(data, occupied)) {
            live.motion = occupied;
            live.valid |= GW_VALID_MOTION;
            changed = true;
        } else {
            ESP_LOGW(TAG, "Occupancy report from node %" PRIu64 " did not decode", node_id);
        }
    } else if (path.mClusterId == kClusterOnOff && path.mAttributeId == kAttrValue) {
        bool on = false;
        if (read_bool(data, on)) {
            live.on = on;
            live.valid |= GW_VALID_ON;
            changed = true;
        }
    } else if (path.mClusterId == kClusterLevel && path.mAttributeId == kAttrValue) {
        uint64_t level = 0;
        if (read_u64(data, level)) {
            live.level = static_cast<uint8_t>(level > 254 ? 254 : level);
            live.valid |= GW_VALID_LEVEL;
            changed = true;
        }
    } else if (path.mClusterId == kClusterColor && path.mAttributeId == kAttrValue) {
        uint64_t hue = 0;
        if (read_u64(data, hue)) {
            live.hue = static_cast<uint8_t>(hue > 254 ? 254 : hue);
            live.valid |= GW_VALID_COLOR;
            changed = true;
        }
    } else if (path.mClusterId == kClusterColor && path.mAttributeId == kAttrSat) {
        uint64_t sat = 0;
        if (read_u64(data, sat)) {
            live.sat = static_cast<uint8_t>(sat > 254 ? 254 : sat);
            live.valid |= GW_VALID_COLOR;
            changed = true;
        }
    } else if (path.mClusterId == kClusterColor && path.mAttributeId == kAttrMireds) {
        uint64_t mireds = 0;
        if (read_u64(data, mireds)) {
            live.mireds = static_cast<uint16_t>(mireds > 1000 ? 1000 : mireds);
            live.valid |= GW_VALID_MIREDS;
            changed = true;
        }
    } else if (path.mClusterId == kClusterTemp && path.mAttributeId == kAttrValue) {
        int64_t temp = 0;
        if (data->Get(temp) == CHIP_NO_ERROR && temp != -32768) {
            live.temp_x100 = static_cast<int16_t>(temp);
            live.valid |= GW_VALID_TEMP;
            changed = true;
        }
    } else if (path.mClusterId == kClusterHumidity && path.mAttributeId == kAttrValue) {
        uint64_t hum = 0;
        if (read_u64(data, hum) && hum != 0xffff) {
            live.hum_x100 = static_cast<uint16_t>(hum);
            live.valid |= GW_VALID_HUMIDITY;
            changed = true;
        }
    } else if (path.mClusterId == kClusterCo2 && path.mAttributeId == kAttrValue) {
        float co2 = 0;
        if (read_float(data, co2)) {
            live.co2 = co2;
            live.valid |= GW_VALID_CO2;
            changed = true;
        }
    } else if (path.mClusterId == kClusterPm25 && path.mAttributeId == kAttrValue) {
        float pm = 0;
        if (read_float(data, pm)) {
            live.pm25 = pm;
            live.valid |= GW_VALID_PM25;
            changed = true;
        }
    } else if (path.mClusterId == kClusterPower && path.mAttributeId == kAttrActivePower) {
        int64_t mw = 0;
        if (data->Get(mw) == CHIP_NO_ERROR) {
            live.power_mw = static_cast<int32_t>(mw);
            live.valid |= GW_VALID_POWER;
            changed = true;
        }
    } else if (path.mClusterId == kClusterPowerSource && path.mAttributeId == kAttrBattery) {
        uint64_t half = 0;
        if (read_u64(data, half)) {
            live.battery_pct = static_cast<uint8_t>(half > 200 ? 100 : half / 2);
            live.valid |= GW_VALID_BATTERY;
            changed = true;
        }
    } else if (path.mClusterId == kClusterSwitch && path.mAttributeId == kAttrSwitchPos) {
        uint64_t pos = 0;
        if (entry->kind == sensor_kind_t::button && read_u64(data, pos)) {
            int button = -1;
            layout = ensure_button_locked(node_id, path.mEndpointId, 0, button) || layout;
            remote_t *remote = find_remote_locked(node_id);
            if (remote && button >= 0) {
                btn_t &one = remote->buttons[button];
                if (pos != 0 && one.last_pos == 0) {
                    note_remote_press(*remote, button, live, 0xffffffff, 0, changed);
                }
                one.last_pos = static_cast<uint8_t>(pos > 255 ? 255 : pos);
            }
            live.button_pos = static_cast<uint8_t>(pos > 255 ? 255 : pos);
        }
    } else if (path.mClusterId == kClusterCover && path.mAttributeId == kAttrLiftPct) {
        uint64_t closed = 0;
        if (read_u64(data, closed)) {
            live.shade_closed_pct = static_cast<uint8_t>(closed > 100 ? 100 : closed);
            live.valid |= GW_VALID_SHADE;
            changed = true;
        }
    } else if (path.mClusterId == kClusterIllum && path.mAttributeId == kAttrValue) {
        uint64_t raw = 0;
        if (read_u64(data, raw) && raw != 0xffff) {
            live.lux = (raw == 0) ? 0
                                  : powf(10.0f, (static_cast<float>(raw) - 1.0f) / 10000.0f);
            if (live.lux > 100000.0f) {
                live.lux = 100000.0f;
            }
            live.valid |= GW_VALID_LUX;
            changed = true;
        }
    } else if (path.mClusterId == kClusterLock && path.mAttributeId == kAttrValue) {
        uint64_t state = 0;
        if (read_u64(data, state)) {
            live.lock_secured = state == 1;
            live.valid |= GW_VALID_LOCK;
            changed = true;
        }
    } else if (path.mClusterId == kClusterThermo && path.mAttributeId == kAttrValue) {
        int64_t temp = 0;
        if (data->Get(temp) == CHIP_NO_ERROR && temp != -32768) {
            live.temp_x100 = static_cast<int16_t>(temp);
            live.valid |= GW_VALID_TEMP;
            changed = true;
        }
    } else if (path.mClusterId == kClusterThermo && path.mAttributeId == kAttrHeatSet) {
        int64_t temp = 0;
        if (data->Get(temp) == CHIP_NO_ERROR) {
            live.heat_set_x100 = static_cast<int16_t>(temp);
            live.valid |= GW_VALID_THERMO;
            changed = true;
        }
    } else if (path.mClusterId == kClusterThermo && path.mAttributeId == kAttrSystemMode) {
        uint64_t mode = 0;
        if (read_u64(data, mode)) {
            if (mode == 4 || mode == 5) {
                live.thermo_mode = 1;
            } else if (mode == 3) {
                live.thermo_mode = 2;
            } else {
                live.thermo_mode = 0;
            }
            live.valid |= GW_VALID_THERMO;
            changed = true;
        }
    } else if (path.mClusterId == kClusterFan && path.mAttributeId == kAttrValue) {
        uint64_t mode = 0;
        if (read_u64(data, mode)) {
            live.on = mode != 0;
            live.valid |= GW_VALID_ON;
            changed = true;
        }
    } else if (path.mClusterId == kClusterSmoke && path.mAttributeId == kAttrSmokeState) {
        uint64_t state = 0;
        if (read_u64(data, state)) {
            live.smoke = state != 0;
            live.valid |= GW_VALID_SMOKE;
            changed = true;
        }
    } else if (path.mClusterId == kClusterSmoke && path.mAttributeId == kAttrCoState) {
        uint64_t state = 0;
        if (read_u64(data, state)) {
            live.co_alarm = state != 0;
            live.valid |= GW_VALID_CO;
            changed = true;
        }
    } else if (s_state[index][0] == 0 || strcmp(s_state[index], "waiting") == 0 ||
               strncmp(s_state[index], "saw ", 4) == 0) {
        snprintf(s_state[index], sizeof(s_state[index]), "saw %04lx/%lx ep%u",
                 static_cast<unsigned long>(path.mClusterId),
                 static_cast<unsigned long>(path.mAttributeId), path.mEndpointId);
    }

    if (changed && memcmp(&before, &live, sizeof(live)) == 0) {
        changed = false;
    }
    if (changed || layout) {
        format_summary(*entry, live, s_state[index], sizeof(s_state[index]));
        ESP_LOGI(TAG, "%s node=%" PRIu64 " %s", kind_name(entry->kind), node_id,
                 s_state[index]);
    }
    unlock_registry();
    if (layout) {
        save_remotes();
    }
    if (changed || layout) {
        homekit_bridge_on_node(node_id);
    }
}

size_t subscriptions_for(sensor_kind_t kind, sub_t *out, size_t cap)
{
    sub_t list[8];
    size_t n = 0;
    auto add = [&](uint32_t cluster, uint32_t attribute) {
        if (n < 8) {
            list[n++] = {cluster, attribute};
        }
    };
    switch (kind) {
    case sensor_kind_t::contact:
        add(kClusterBoolean, kAttrValue);
        add(kClusterOccupancy, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::motion:
        add(kClusterOccupancy, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::leak:
        add(kClusterBoolean, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::climate:
        add(kClusterTemp, kAttrValue);
        add(kClusterHumidity, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::air:
        add(kClusterCo2, kAttrValue);
        add(kClusterPm25, kAttrValue);
        add(kClusterTemp, kAttrValue);
        add(kClusterHumidity, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::plug:
        add(kClusterOnOff, kAttrValue);
        add(kClusterPower, kAttrActivePower);
        break;
    case sensor_kind_t::light:
        add(kClusterOnOff, kAttrValue);
        add(kClusterLevel, kAttrValue);
        break;
    case sensor_kind_t::light_ct:
        add(kClusterOnOff, kAttrValue);
        add(kClusterLevel, kAttrValue);
        add(kClusterColor, kAttrMireds);
        break;
    case sensor_kind_t::light_color:
        add(kClusterOnOff, kAttrValue);
        add(kClusterLevel, kAttrValue);
        add(kClusterColor, kAttrValue);
        add(kClusterColor, kAttrSat);
        add(kClusterColor, kAttrMireds);
        break;
    case sensor_kind_t::button:
        add(kClusterSwitch, kAttrSwitchPos);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::shade:
        add(kClusterCover, kAttrLiftPct);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::onoff:
    case sensor_kind_t::wall:
        add(kClusterOnOff, kAttrValue);
        break;
    case sensor_kind_t::occupancy:
        add(kClusterOccupancy, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::lux:
        add(kClusterIllum, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::lock:
        add(kClusterLock, kAttrValue);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::thermostat:
        add(kClusterThermo, kAttrValue);
        add(kClusterThermo, kAttrHeatSet);
        add(kClusterThermo, kAttrSystemMode);
        add(kClusterPowerSource, kAttrBattery);
        break;
    case sensor_kind_t::fan:
        add(kClusterFan, kAttrValue);
        add(kClusterOnOff, kAttrValue);
        break;
    case sensor_kind_t::smoke:
        add(kClusterSmoke, kAttrSmokeState);
        add(kClusterSmoke, kAttrCoState);
        add(kClusterPowerSource, kAttrBattery);
        break;
    default:
        break;
    }
    size_t copy = n < cap ? n : cap;
    for (size_t i = 0; i < copy; ++i) {
        out[i] = list[i];
    }
    return n;
}

void fill_paths(const node_entry_t &entry, chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> &attrs,
                chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> &events)
{
    // Switch cluster events. A remote does not leave CurrentPosition at the
    // pressed value, so these have to be subscribed on their own, the same
    // way occupancy and contact are, or the press is easy to miss.
    constexpr uint32_t kSwitchEvents[] = {
        0x00, // Latched
        0x01, // InitialPress
        0x02, // LongPress
        0x03, // ShortRelease
        0x04, // LongRelease
        0x05, // MultiPressOngoing
        0x06, // MultiPressComplete
    };
    sub_t subs[8];
    size_t n = subscriptions_for(entry.kind, subs, 8);
    attrs.Alloc(n);
    size_t event_count = 0;
    bool switch_events = false;
    for (size_t i = 0; i < n; ++i) {
        if (subs[i].cluster == kClusterBoolean || subs[i].cluster == kClusterOccupancy) {
            event_count++;
        } else if (subs[i].cluster == kClusterSwitch && !switch_events) {
            switch_events = true;
            event_count += 1 + (sizeof(kSwitchEvents) / sizeof(kSwitchEvents[0]));
        }
    }
    events.Alloc(event_count);
    size_t event_index = 0;
    switch_events = false;
    for (size_t i = 0; i < n; ++i) {
        if (attrs.Get()) {
            attrs[i] = chip::app::AttributePathParams(kWildcardEndpoint, subs[i].cluster, subs[i].attribute);
        }
        if (!events.Get()) {
            continue;
        }
        if (subs[i].cluster == kClusterBoolean || subs[i].cluster == kClusterOccupancy) {
            // Info-priority events, including a contact StateChange, are queued
            // unless the subscription marks them urgent.
            events[event_index++] =
                chip::app::EventPathParams(kWildcardEndpoint, subs[i].cluster, kWildcardAttribute, true);
        } else if (subs[i].cluster == kClusterSwitch && !switch_events) {
            switch_events = true;
            events[event_index++] =
                chip::app::EventPathParams(kWildcardEndpoint, subs[i].cluster, kWildcardAttribute, true);
            for (uint32_t event_id : kSwitchEvents) {
                events[event_index++] =
                    chip::app::EventPathParams(kWildcardEndpoint, subs[i].cluster, event_id, true);
            }
        }
    }
}

esp_err_t send_watch(const node_entry_t &entry)
{
    if (watch_is_inflight(entry.node_id) || watch_is_up(entry.node_id)) {
        return ESP_OK;
    }
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> attrs;
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> events;
    fill_paths(entry, attrs, events);
    if (!attrs.Get() || attrs.AllocatedSize() == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // Min 0 lets a change go out at once. Max 10 stops a device from holding
    // that change until the old one-hour ceiling. Switch presses, occupancy,
    // and contact events are marked urgent in fill_paths, so a press or
    // motion is delivered immediately instead of waiting for the next poll.
    auto *command = chip::Platform::New<esp_matter::controller::subscribe_command>(
        entry.node_id, std::move(attrs), std::move(events), 0, 10, true, sensor_attribute_report,
        sensor_event_report, on_subscribe_done, on_subscribe_fail, on_subscribe_established);
    if (!command) {
        ESP_LOGW(TAG, "No memory to subscribe node=%" PRIu64, entry.node_id);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Subscribing to %s node=%" PRIu64, kind_name(entry.kind), entry.node_id);
    matter_sniff_push(MATTER_SNIFF_TX, entry.node_id, 0, entry.endpoint_id, 0, "subscribe");
    watch_begin(entry.node_id, command);
    esp_err_t err = command->send_command();
    if (err != ESP_OK) {
        watch_down(entry.node_id);
    }
    return err;
}

esp_err_t send_contact_read(const node_entry_t &entry)
{
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> attrs;
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> events;
    if (!attrs.Alloc(1) || !attrs.Get()) {
        return ESP_ERR_NO_MEM;
    }
    attrs[0] = chip::app::AttributePathParams(kWildcardEndpoint, kClusterBoolean, kAttrValue);
    matter_sniff_push(MATTER_SNIFF_TX, entry.node_id, kClusterBoolean, entry.endpoint_id, kAttrValue,
                      "contact");
    auto *command = chip::Platform::New<esp_matter::controller::read_command>(
        entry.node_id, std::move(attrs), std::move(events), sensor_attribute_report, nullptr, nullptr);
    if (!command) {
        return ESP_ERR_NO_MEM;
    }
    return command->send_command();
}

esp_err_t send_read(const node_entry_t &entry)
{
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> attrs;
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> events;
    fill_paths(entry, attrs, events);
    if (!attrs.Get() || attrs.AllocatedSize() == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    // Attribute poll only. Reading events replays the device log and would
    // push every stored button press to HomeKit again.
    events.Free();
    matter_sniff_push(MATTER_SNIFF_TX, entry.node_id, 0, entry.endpoint_id, 0, "read");
    auto *command = chip::Platform::New<esp_matter::controller::read_command>(
        entry.node_id, std::move(attrs), std::move(events), sensor_attribute_report, nullptr,
        nullptr);
    if (!command) {
        return ESP_ERR_NO_MEM;
    }
    return command->send_command();
}

esp_err_t write_attr(uint64_t node_id, uint16_t endpoint, uint32_t cluster,
                     uint32_t attribute, const char *json)
{
    auto *request = chip::Platform::New<esp_matter::controller::write_command>(
        node_id, endpoint, cluster, attribute, json);
    if (!request) {
        return ESP_ERR_NO_MEM;
    }
    return request->send_command();
}

esp_err_t invoke_command(uint64_t node_id, uint16_t endpoint, uint32_t cluster,
                         uint32_t command, const char *json)
{
    auto *request = chip::Platform::New<esp_matter::controller::cluster_command>(
        node_id, endpoint, cluster, command, json);
    if (!request) {
        return ESP_ERR_NO_MEM;
    }
    return request->send_command();
}

node_entry_t *find_node(uint64_t node_id)
{
    for (size_t i = 0; i < s_registry.count; ++i) {
        if (s_registry.entries[i].node_id == node_id) {
            return &s_registry.entries[i];
        }
    }
    return nullptr;
}

void json_escape(const char *in, char *out, size_t out_len)
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
        }
        if ((unsigned char)c < 0x20) {
            c = ' ';
        }
        out[o++] = c;
    }
    out[o] = 0;
}

esp_err_t remember_node(uint64_t node_id, uint16_t endpoint, sensor_kind_t kind,
                        const char *name)
{
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    bool created = false;
    if (!entry) {
        if (s_registry.count == kMaxNodes) {
            unlock_registry();
            return ESP_ERR_NO_MEM;
        }
        entry = &s_registry.entries[s_registry.count];
        memset(entry, 0, sizeof(*entry));
        entry->node_id = node_id;
        created = true;
    }
    size_t index = static_cast<size_t>(entry - s_registry.entries);
    entry->endpoint_id = endpoint;
    entry->kind = kind;
    snprintf(entry->name, sizeof(entry->name), "%s",
             (name && name[0]) ? name : kind_name(kind));
    s_state[index][0] = 0;
    memset(&s_live[index], 0, sizeof(s_live[index]));
    if (created) {
        ++s_registry.count;
    }
    bool grew = false;
    if (kind == sensor_kind_t::button && endpoint != 0) {
        int button = -1;
        grew = ensure_button_locked(node_id, endpoint, 0, button);
    }
    node_entry_t copy = *entry;
    unlock_registry();
    if (grew) {
        save_remotes();
    }

    esp_err_t err = save_registry();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Device saved; starting subscription");
        err = send_watch(copy);
        send_read(copy);
        homekit_bridge_attach(node_id);
    }
    return err;
}

esp_err_t forget_node(uint64_t node_id)
{
    lock_registry();
    for (size_t i = 0; i < s_registry.count; ++i) {
        if (s_registry.entries[i].node_id != node_id) {
            continue;
        }
        memmove(&s_registry.entries[i], &s_registry.entries[i + 1],
                (s_registry.count - i - 1) * sizeof(node_entry_t));
        memmove(&s_state[i], &s_state[i + 1],
                (s_registry.count - i - 1) * sizeof(s_state[0]));
        memmove(&s_live[i], &s_live[i + 1],
                (s_registry.count - i - 1) * sizeof(s_live[0]));
        --s_registry.count;
        memset(&s_registry.entries[s_registry.count], 0, sizeof(node_entry_t));
        s_state[s_registry.count][0] = 0;
        memset(&s_live[s_registry.count], 0, sizeof(s_live[0]));
        drop_remote_locked(node_id);
        unlock_registry();
        save_remotes();
        watch_forget(node_id);
        using esp_matter::lock::status_t;
        status_t locked = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
        if (locked != esp_matter::lock::FAILED) {
            esp_matter::controller::send_shutdown_subscriptions(node_id);
            matter_sniff_push(MATTER_SNIFF_TX, node_id, 0, 0, 0, "shutdown");
            if (locked == esp_matter::lock::SUCCESS) {
                esp_matter::lock::chip_stack_unlock();
            }
        }
        homekit_bridge_detach(node_id);
        return save_registry();
    }
    unlock_registry();
    return ESP_ERR_NOT_FOUND;
}

void print_help()
{
    ESP_LOGI(TAG, "matter esp gateway nodes");
    ESP_LOGI(TAG, "matter esp gateway remember <node-id> <endpoint> <kind> [name]");
    ESP_LOGI(TAG, "kinds: contact motion occupancy leak climate air illuminance smoke "
                  "onoff light light_ct light_color plug switch fan button shade lock thermostat");
    ESP_LOGI(TAG, "matter esp gateway forget <node-id>");
    ESP_LOGI(TAG, "matter esp gateway discover <node-id>");
    ESP_LOGI(TAG, "matter esp gateway read <node-id>");
    ESP_LOGI(TAG, "matter esp gateway watch <node-id>");
    ESP_LOGI(TAG, "matter esp gateway watch-all");
}

esp_err_t gateway_command(int argc, char **argv)
{
    if (argc < 1 || strcmp(argv[0], "help") == 0) {
        print_help();
        return ESP_OK;
    }

    if (strcmp(argv[0], "nodes") == 0) {
        ESP_LOGI(TAG, "%u registered sensor(s)", s_registry.count);
        for (size_t i = 0; i < s_registry.count; ++i) {
            const node_entry_t &entry = s_registry.entries[i];
            ESP_LOGI(TAG,
                     "[%u] node=0x%016" PRIx64 " endpoint=%u kind=%s name='%s'",
                     static_cast<unsigned>(i), entry.node_id, entry.endpoint_id,
                     kind_name(entry.kind), entry.name);
        }
        return ESP_OK;
    }

    if (argc < 2) {
        print_help();
        return ESP_ERR_INVALID_ARG;
    }

    char *end = nullptr;
    uint64_t node_id = strtoull(argv[1], &end, 0);
    if (!argv[1][0] || (end && *end)) {
        ESP_LOGE(TAG, "Invalid node id '%s'", argv[1]);
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(argv[0], "remember") == 0) {
        if (argc < 4) {
            print_help();
            return ESP_ERR_INVALID_ARG;
        }
        unsigned long endpoint = strtoul(argv[2], &end, 0);
        sensor_kind_t kind;
        if ((end && *end) || endpoint > UINT16_MAX || !parse_kind(argv[3], kind)) {
            ESP_LOGE(TAG, "Endpoint or sensor kind is invalid");
            return ESP_ERR_INVALID_ARG;
        }

        return remember_node(node_id, static_cast<uint16_t>(endpoint), kind,
                             argc >= 5 ? argv[4] : nullptr);
    }

    if (strcmp(argv[0], "forget") == 0) {
        return forget_node(node_id);
    }

    if (strcmp(argv[0], "discover") == 0) {
        auto *command = chip::Platform::New<esp_matter::controller::read_command>(
            node_id, kWildcardEndpoint, kDescriptorCluster, kWildcardAttribute,
            esp_matter::controller::READ_ATTRIBUTE, nullptr, nullptr, nullptr);
        return command ? command->send_command() : ESP_ERR_NO_MEM;
    }

    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        ESP_LOGE(TAG, "Unknown node; use 'gateway remember' first");
        return ESP_ERR_NOT_FOUND;
    }
    if (strcmp(argv[0], "read") == 0) {
        return send_read(*entry);
    }
    if (strcmp(argv[0], "watch") == 0) {
        return send_watch(*entry);
    }

    print_help();
    return ESP_ERR_INVALID_ARG;
}

esp_err_t watch_all_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < s_registry.count; ++i) {
        esp_err_t err = send_watch(s_registry.entries[i]);
        if (err != ESP_OK) {
            result = err;
        }
    }
    return result;
}

esp_err_t command_dispatch(int argc, char **argv)
{
    if (argc > 0 && strcmp(argv[0], "watch-all") == 0) {
        return watch_all_command(argc - 1, argv + 1);
    }
    return gateway_command(argc, argv);
}

} // namespace

struct peer_slot_t {
    uint64_t node_id;
    otIp6Address ip;
    bool have;
};
static peer_slot_t s_peers[kMaxNodes];
static SemaphoreHandle_t s_peer_lock;

esp_err_t gateway_nodes_init(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    s_registry = {.version = kRegistryVersion};
    memset(s_state, 0, sizeof(s_state));
    memset(s_live, 0, sizeof(s_live));
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (!s_peer_lock) {
        s_peer_lock = xSemaphoreCreateMutex();
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_loaded = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t size = sizeof(s_registry);
    err = nvs_get_blob(handle, kNvsKey, &s_registry, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_registry = {.version = kRegistryVersion};
        err = ESP_OK;
    } else if (err != ESP_OK || size != sizeof(s_registry) ||
               s_registry.version != kRegistryVersion ||
               s_registry.count > kMaxNodes) {
        ESP_LOGW(TAG, "Ignoring incompatible node registry");
        s_registry = {.version = kRegistryVersion};
        err = ESP_OK;
    }
    s_loaded = true;
    load_remotes();
    bool seeded = false;
    lock_registry();
    for (size_t i = 0; i < s_registry.count; ++i) {
        if (s_registry.entries[i].kind != sensor_kind_t::button) {
            continue;
        }
        int button = -1;
        seeded = ensure_button_locked(s_registry.entries[i].node_id, s_registry.entries[i].endpoint_id, 0,
                                      button) ||
                 seeded;
    }
    unlock_registry();
    if (seeded) {
        save_remotes();
    }
    return err;
}

void copy_nodes(node_entry_t *out, size_t &count)
{
    lock_registry();
    count = s_registry.count;
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_registry.entries[i];
    }
    unlock_registry();
}

static void cache_peer_ips(const node_entry_t *nodes, size_t count);

void cache_only(const node_entry_t *, size_t) {}

void with_chip_stack(void (*fn)(const node_entry_t *, size_t), const node_entry_t *nodes, size_t count)
{
    // Snapshot every node before the Matter lock. A retry passes only the
    // device it is starting, and caching that subset alone cleared the
    // addresses used for the others' signal strength.
    node_entry_t all[kMaxNodes];
    size_t all_count = 0;
    copy_nodes(all, all_count);

    using esp_matter::lock::status_t;
    status_t locked = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    if (locked == esp_matter::lock::FAILED) {
        ESP_LOGW(TAG, "Matter stack is busy");
        return;
    }
    fn(nodes, count);
    if (all_count > 0) {
        cache_peer_ips(all, all_count);
    }
    if (locked == esp_matter::lock::SUCCESS) {
        esp_matter::lock::chip_stack_unlock();
    }
}

void refresh_locked(const node_entry_t *nodes, size_t count)
{
    // The rest follow from retry_watch, two at a time. Starting every sensor
    // in the same instant made the secure sessions time out.
    if (count > 0) {
        esp_err_t err = send_watch(nodes[0]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Subscribe start failed: %s", esp_err_to_name(err));
        }
    }
}

void rewatch_locked(const node_entry_t *nodes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        esp_matter::controller::send_shutdown_subscriptions(nodes[i].node_id);
        watch_down(nodes[i].node_id);
        send_watch(nodes[i]);
    }
}

bool contact_is_known(uint64_t node_id)
{
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    bool known = false;
    if (entry) {
        size_t index = static_cast<size_t>(entry - s_registry.entries);
        known = (s_live[index].valid & GW_VALID_CONTACT) != 0;
    }
    unlock_registry();
    return known;
}

void poll_locked(const node_entry_t *nodes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (watch_is_up(nodes[i].node_id)) {
            send_read(nodes[i]);
        }
    }
}

void poll_contacts_locked(const node_entry_t *nodes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        // This sensor keeps StateValue at closed and does not push when the
        // magnet moves. Ask it directly. Motion and buttons already push.
        if (watch_is_up(nodes[i].node_id) && contact_is_known(nodes[i].node_id)) {
            send_contact_read(nodes[i]);
        }
    }
}

void retry_watch_locked(const node_entry_t *nodes, size_t count)
{
    if (count == 0) {
        return;
    }
    send_watch(nodes[0]);
}

void replace_watch_locked(const node_entry_t *nodes, size_t count)
{
    if (count == 0) {
        return;
    }
    esp_matter::controller::send_shutdown_subscriptions(nodes[0].node_id);
    send_watch(nodes[0]);
}

void gateway_nodes_refresh(void)
{
    gateway_nodes_init();
    node_entry_t nodes[kMaxNodes];
    size_t count = 0;
    copy_nodes(nodes, count);
    with_chip_stack(refresh_locked, nodes, count);
}

void gateway_nodes_poll(void)
{
    if (!s_loaded) {
        return;
    }
    node_entry_t nodes[kMaxNodes];
    size_t count = 0;
    copy_nodes(nodes, count);
    with_chip_stack(poll_locked, nodes, count);
}

void gateway_nodes_poll_contacts(void)
{
    if (!s_loaded) {
        return;
    }
    node_entry_t nodes[kMaxNodes];
    size_t count = 0;
    copy_nodes(nodes, count);
    with_chip_stack(poll_contacts_locked, nodes, count);
}

void gateway_nodes_retry_watch(void)
{
    if (!s_loaded) {
        gateway_nodes_init();
    }
    node_entry_t nodes[kMaxNodes];
    size_t count = 0;
    copy_nodes(nodes, count);
    if (count == 0) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    constexpr int kMaxInflight = 2;
    constexpr int64_t kStuckUs = 120LL * 1000 * 1000;
    constexpr int64_t kStaggerUs = 2LL * 1000 * 1000;
    int inflight = 0;
    int best = -1;
    int stuck = -1;
    int64_t best_since = INT64_MAX;
    lock_registry();
    if (s_last_watch_us != 0 && now - s_last_watch_us < kStaggerUs) {
        unlock_registry();
        with_chip_stack(cache_only, nodes, count);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        watch_t *slot = find_watch_locked(nodes[i].node_id);
        if (slot && slot->state == watch_state_t::inflight) {
            if (now - slot->since_us < kStuckUs) {
                inflight++;
                continue;
            }
            slot->cmd = nullptr;
            slot->state = watch_state_t::down;
            slot->since_us = now;
            stuck = static_cast<int>(i);
        }
        if (slot && slot->state == watch_state_t::up) {
            continue;
        }
        int64_t since = slot ? slot->since_us : 0;
        if (since < best_since) {
            best_since = since;
            best = static_cast<int>(i);
        }
    }
    unlock_registry();
    if (stuck >= 0) {
        node_entry_t one = nodes[stuck];
        with_chip_stack(replace_watch_locked, &one, 1);
        return;
    }
    if (inflight >= kMaxInflight || best < 0) {
        with_chip_stack(cache_only, nodes, count);
        return;
    }
    node_entry_t one = nodes[best];
    with_chip_stack(retry_watch_locked, &one, 1);
}

uint64_t gateway_nodes_next_id(void)
{
    uint64_t next = 1234;
    lock_registry();
    for (size_t i = 0; i < s_registry.count; ++i) {
        if (s_registry.entries[i].node_id >= next) {
            next = s_registry.entries[i].node_id + 1;
        }
    }
    unlock_registry();
    return next;
}

esp_err_t gateway_nodes_remember(uint64_t node_id, uint16_t endpoint_id,
                                 const char *kind, const char *name)
{
    sensor_kind_t parsed;
    if (!kind || !parse_kind(kind, parsed) || node_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return remember_node(node_id, endpoint_id, parsed, name);
}

esp_err_t gateway_nodes_forget(uint64_t node_id)
{
    return forget_node(node_id);
}

esp_err_t gateway_nodes_update(uint64_t node_id, const char *name, const char *kind)
{
    if (node_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_kind_t parsed = sensor_kind_t::contact;
    const bool set_kind = kind && kind[0];
    if (set_kind && !parse_kind(kind, parsed)) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    const bool kind_changed = set_kind && entry->kind != parsed;
    if (kind_changed) {
        entry->kind = parsed;
    }
    if (name && name[0]) {
        snprintf(entry->name, sizeof(entry->name), "%s", name);
    }
    node_entry_t copy = *entry;
    unlock_registry();

    esp_err_t err = save_registry();
    if (err != ESP_OK) {
        return err;
    }
    if (kind_changed && parsed == sensor_kind_t::button) {
        lock_registry();
        int button = -1;
        bool grew = ensure_button_locked(node_id, copy.endpoint_id, 0, button);
        unlock_registry();
        if (grew) {
            save_remotes();
        }
    }
    if (kind_changed) {
        with_chip_stack(rewatch_locked, &copy, 1);
        homekit_bridge_detach(node_id);
        homekit_bridge_attach(node_id);
    } else if (name && name[0]) {
        homekit_bridge_rename(node_id, copy.name);
    }
    return ESP_OK;
}

esp_err_t gateway_nodes_set_buttons(uint64_t node_id, const gateway_button_edit_t *buttons, uint8_t count)
{
    if (node_id == 0 || !buttons) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_registry();
    remote_t *remote = find_remote_locked(node_id);
    if (!remote || remote->count == 0) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t n = count < remote->count ? count : remote->count;
    for (uint8_t i = 0; i < n; ++i) {
        remote->buttons[i].mode = buttons[i].mode ? 1 : 0;
        if (buttons[i].name[0]) {
            snprintf(remote->buttons[i].name, sizeof(remote->buttons[i].name), "%s", buttons[i].name);
        }
    }
    unlock_registry();
    save_remotes();
    homekit_bridge_detach(node_id);
    homekit_bridge_attach(node_id);
    return ESP_OK;
}

esp_err_t gateway_nodes_set_button_on(uint64_t node_id, uint8_t index, bool on)
{
    lock_registry();
    remote_t *remote = find_remote_locked(node_id);
    if (!remote || index >= remote->count) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    remote->buttons[index].on = on ? 1 : 0;
    node_entry_t *entry = find_node(node_id);
    if (entry) {
        size_t slot = static_cast<size_t>(entry - s_registry.entries);
        format_summary(*entry, s_live[slot], s_state[slot], sizeof(s_state[slot]));
    }
    unlock_registry();
    save_remotes();
    return ESP_OK;
}

esp_err_t gateway_nodes_read_now(uint64_t node_id)
{
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    node_entry_t copy = *entry;
    unlock_registry();
    with_chip_stack(poll_locked, &copy, 1);
    return ESP_OK;
}

bool lookup_endpoint(uint64_t node_id, uint16_t &endpoint)
{
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return false;
    }
    endpoint = entry->endpoint_id;
    unlock_registry();
    return true;
}

int gateway_nodes_count(void)
{
    lock_registry();
    int count = s_registry.count;
    unlock_registry();
    return count;
}

esp_err_t gateway_nodes_copy(int index, gateway_node_view_t *out)
{
    if (!out || index < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_registry();
    if (static_cast<size_t>(index) >= s_registry.count) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    fill_view(s_registry.entries[index], s_live[index], out);
    unlock_registry();
    return ESP_OK;
}

esp_err_t gateway_nodes_copy_id(uint64_t node_id, gateway_node_view_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_registry();
    node_entry_t *entry = find_node(node_id);
    if (!entry) {
        unlock_registry();
        return ESP_ERR_NOT_FOUND;
    }
    size_t index = static_cast<size_t>(entry - s_registry.entries);
    fill_view(*entry, s_live[index], out);
    unlock_registry();
    return ESP_OK;
}

esp_err_t gateway_nodes_set_on(uint64_t node_id, bool on)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    return invoke_command(node_id, endpoint, kClusterOnOff, on ? 1 : 0, "{}");
}

esp_err_t gateway_nodes_set_level_pct(uint64_t node_id, int percent)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    unsigned level = static_cast<unsigned>(percent) * 254 / 100;
    char json[96];
    snprintf(json, sizeof(json),
             "{\"0:UINT\":%u,\"1:UINT\":0,\"2:UINT\":0,\"3:UINT\":0}", level);
    return invoke_command(node_id, endpoint, kClusterLevel, 0x04, json);
}

esp_err_t gateway_nodes_set_hue_sat(uint64_t node_id, float hue_deg, float saturation_pct)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (hue_deg < 0) {
        hue_deg = 0;
    } else if (hue_deg > 360) {
        hue_deg = 360;
    }
    if (saturation_pct < 0) {
        saturation_pct = 0;
    } else if (saturation_pct > 100) {
        saturation_pct = 100;
    }
    unsigned hue = static_cast<unsigned>(hue_deg * 254.0f / 360.0f);
    unsigned sat = static_cast<unsigned>(saturation_pct * 254.0f / 100.0f);
    char json[128];
    snprintf(json, sizeof(json),
             "{\"0:UINT\":%u,\"1:UINT\":%u,\"2:UINT\":0,\"3:UINT\":0,\"4:UINT\":0}",
             hue, sat);
    return invoke_command(node_id, endpoint, kClusterColor, 0x06, json);
}

esp_err_t gateway_nodes_set_mireds(uint64_t node_id, int mireds)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (mireds < 0) {
        mireds = 0;
    }
    char json[96];
    snprintf(json, sizeof(json),
             "{\"0:UINT\":%d,\"1:UINT\":0,\"2:UINT\":0,\"3:UINT\":0}", mireds);
    return invoke_command(node_id, endpoint, kClusterColor, 0x0a, json);
}

esp_err_t gateway_nodes_set_shade_open_pct(uint64_t node_id, int open_pct)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (open_pct < 0) {
        open_pct = 0;
    } else if (open_pct > 100) {
        open_pct = 100;
    }
    unsigned closed_100ths = static_cast<unsigned>(100 - open_pct) * 100;
    char json[48];
    snprintf(json, sizeof(json), "{\"0:UINT\":%u}", closed_100ths);
    return invoke_command(node_id, endpoint, kClusterCover, 0x05, json);
}

esp_err_t gateway_nodes_set_lock(uint64_t node_id, bool secured)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    return invoke_command(node_id, endpoint, kClusterLock, secured ? 0x00 : 0x01, "{}");
}

esp_err_t gateway_nodes_set_thermo_mode(uint64_t node_id, int hap_mode)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    unsigned mode = 0;
    if (hap_mode == 1) {
        mode = 4;
    } else if (hap_mode == 2) {
        mode = 3;
    }
    char json[32];
    snprintf(json, sizeof(json), "{\"0:UINT\":%u}", mode);
    return write_attr(node_id, endpoint, kClusterThermo, kAttrSystemMode, json);
}

esp_err_t gateway_nodes_set_heat_setpoint(uint64_t node_id, float celsius)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (celsius < 5) {
        celsius = 5;
    } else if (celsius > 35) {
        celsius = 35;
    }
    int hundredths = static_cast<int>(celsius * 100.0f);
    char json[32];
    snprintf(json, sizeof(json), "{\"0:INT\":%d}", hundredths);
    esp_err_t heat = write_attr(node_id, endpoint, kClusterThermo, kAttrHeatSet, json);
    esp_err_t cool = write_attr(node_id, endpoint, kClusterThermo, kAttrCoolSet, json);
    return heat != ESP_OK ? heat : cool;
}

esp_err_t gateway_nodes_set_fan(uint64_t node_id, bool on)
{
    uint16_t endpoint = 0;
    if (!lookup_endpoint(node_id, endpoint)) {
        return ESP_ERR_NOT_FOUND;
    }
    char json[32];
    snprintf(json, sizeof(json), "{\"0:UINT\":%u}", on ? 4u : 0u);
    esp_err_t mode = write_attr(node_id, endpoint, kClusterFan, kAttrValue, json);
    esp_err_t power = invoke_command(node_id, endpoint, kClusterOnOff, on ? 1 : 0, "{}");
    return mode == ESP_OK ? mode : power;
}

constexpr int kRssiInvalid = 127;

static bool same_ip(const otIp6Address &a, const otIp6Address &b)
{
    return memcmp(a.mFields.m8, b.mFields.m8, sizeof(a.mFields.m8)) == 0;
}

static int pick_rssi(int8_t last, int8_t average)
{
    if (last != kRssiInvalid) {
        return last;
    }
    if (average != kRssiInvalid) {
        return average;
    }
    return kRssiInvalid;
}

static int thread_rssi_for_ip(otInstance *instance, const otIp6Address &peer)
{
    uint16_t max_children = otThreadGetMaxAllowedChildren(instance);
    if (max_children > 32) {
        max_children = 32;
    }
    for (uint16_t i = 0; i < max_children; ++i) {
        otChildInfo child;
        if (otThreadGetChildInfoByIndex(instance, i, &child) != OT_ERROR_NONE) {
            continue;
        }
        otChildIp6AddressIterator it = OT_CHILD_IP6_ADDRESS_ITERATOR_INIT;
        otIp6Address addr;
        while (otThreadGetChildNextIp6Address(instance, i, &it, &addr) == OT_ERROR_NONE) {
            if (same_ip(addr, peer)) {
                return pick_rssi(child.mLastRssi, child.mAverageRssi);
            }
        }
    }

    uint16_t rloc = 0;
    bool have_rloc = false;
    otCacheEntryIterator cache_it;
    memset(&cache_it, 0, sizeof(cache_it));
    otCacheEntryInfo cache;
    while (otThreadGetNextCacheEntry(instance, &cache, &cache_it) == OT_ERROR_NONE) {
        if (same_ip(cache.mTarget, peer) || (cache.mValidLastTrans && same_ip(cache.mMeshLocalEid, peer))) {
            rloc = cache.mRloc16;
            have_rloc = true;
            break;
        }
    }

    otNeighborInfoIterator nit = OT_NEIGHBOR_INFO_ITERATOR_INIT;
    otNeighborInfo info;
    while (otThreadGetNextNeighborInfo(instance, &nit, &info) == OT_ERROR_NONE) {
        bool match = have_rloc && info.mRloc16 == rloc;
        if (!match) {
            uint8_t iid[8];
            memcpy(iid, info.mExtAddress.m8, sizeof(iid));
            iid[0] ^= 0x02;
            match = memcmp(peer.mFields.m8 + 8, iid, sizeof(iid)) == 0;
        }
        if (match) {
            return pick_rssi(info.mLastRssi, info.mAverageRssi);
        }
    }
    return kRssiInvalid;
}

static bool peer_ip_for_node(uint64_t node_id, otIp6Address &out)
{
    auto *commissioner = esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
    if (!commissioner || commissioner->SessionMgr() == nullptr) {
        return false;
    }
    const chip::FabricIndex fabric = commissioner->GetFabricIndex();
    auto found = commissioner->SessionMgr()->FindSecureSessionForNode(chip::ScopedNodeId(node_id, fabric));
    if (!found.HasValue()) {
        return false;
    }
    chip::Transport::SecureSession *secure = found.Value()->AsSecureSession();
    if (secure == nullptr) {
        return false;
    }
    const chip::Transport::PeerAddress &addr = secure->GetPeerAddress();
    if (addr.GetTransportType() != chip::Transport::Type::kUdp || !addr.GetIPAddress().IsIPv6()) {
        return false;
    }
    /* Addr[] is stored in wire order. Copying the bytes matches otIp6Address. */
    memcpy(out.mFields.m8, addr.GetIPAddress().Addr, sizeof(out.mFields.m8));
    return true;
}

static void cache_peer_ips(const node_entry_t *nodes, size_t count)
{
    peer_slot_t fresh[kMaxNodes] = {};
    if (count > kMaxNodes) {
        count = kMaxNodes;
    }
    for (size_t i = 0; i < count; ++i) {
        fresh[i].node_id = nodes[i].node_id;
        fresh[i].have = peer_ip_for_node(nodes[i].node_id, fresh[i].ip);
    }
    if (s_peer_lock) {
        xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    }
    memcpy(s_peers, fresh, sizeof(s_peers));
    if (s_peer_lock) {
        xSemaphoreGive(s_peer_lock);
    }
}

esp_err_t gateway_nodes_write_json(char *out, size_t out_len)
{
    if (!out || out_len < 3) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t used = 0;
    int wrote = snprintf(out, out_len, "[");
    if (wrote < 0 || static_cast<size_t>(wrote) >= out_len) {
        return ESP_ERR_NO_MEM;
    }
    used = static_cast<size_t>(wrote);

    uint64_t ids[kMaxNodes];
    int rssi_by_id[kMaxNodes];
    peer_slot_t peers[kMaxNodes];
    size_t id_count = 0;
    lock_registry();
    id_count = s_registry.count;
    if (id_count > kMaxNodes) {
        id_count = kMaxNodes;
    }
    for (size_t i = 0; i < id_count; ++i) {
        ids[i] = s_registry.entries[i].node_id;
        rssi_by_id[i] = kRssiInvalid;
    }
    unlock_registry();
    memset(peers, 0, sizeof(peers));
    if (s_peer_lock && xSemaphoreTake(s_peer_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(peers, s_peers, sizeof(peers));
        xSemaphoreGive(s_peer_lock);
    }
    otInstance *instance = esp_openthread_get_instance();
    if (instance != nullptr && esp_openthread_lock_acquire(pdMS_TO_TICKS(40))) {
        for (size_t i = 0; i < id_count; ++i) {
            for (size_t n = 0; n < kMaxNodes; ++n) {
                if (peers[n].have && peers[n].node_id == ids[i]) {
                    rssi_by_id[i] = thread_rssi_for_ip(instance, peers[n].ip);
                    break;
                }
            }
        }
        esp_openthread_lock_release();
    }

    lock_registry();
    for (size_t i = 0; i < s_registry.count; ++i) {
        const node_entry_t &entry = s_registry.entries[i];
        char name[48];
        char state[96];
        json_escape(entry.name, name, sizeof(name));
        json_escape(s_state[i][0] ? s_state[i] : "waiting", state, sizeof(state));
        const int battery = (s_live[i].valid & GW_VALID_BATTERY) ? s_live[i].battery_pct : -1;
        const double temp_c =
            (s_live[i].valid & GW_VALID_TEMP) ? s_live[i].temp_x100 / 100.0 : -999.0;
        const double humidity =
            (s_live[i].valid & GW_VALID_HUMIDITY) ? s_live[i].hum_x100 / 100.0 : -1.0;
        char buttons[512];
        buttons[0] = 0;
        size_t bu = 0;
        const remote_t *remote = find_remote_locked(entry.node_id);
        if (remote && remote->count > 0) {
            bu = static_cast<size_t>(snprintf(buttons, sizeof(buttons), ",\"buttons\":["));
            for (uint8_t b = 0; b < remote->count && bu + 8 < sizeof(buttons); ++b) {
                char bname[48];
                json_escape(remote->buttons[b].name, bname, sizeof(bname));
                int n = snprintf(buttons + bu, sizeof(buttons) - bu,
                                 "%s{\"name\":\"%s\",\"mode\":%u,\"on\":%u,\"endpoint\":%u}",
                                 b ? "," : "", bname, remote->buttons[b].mode, remote->buttons[b].on,
                                 remote->buttons[b].endpoint);
                if (n < 0 || static_cast<size_t>(n) >= sizeof(buttons) - bu) {
                    break;
                }
                bu += static_cast<size_t>(n);
            }
            if (bu + 2 < sizeof(buttons)) {
                buttons[bu++] = ']';
                buttons[bu] = 0;
            }
        }
        int rssi = kRssiInvalid;
        for (size_t n = 0; n < id_count; ++n) {
            if (ids[n] == entry.node_id) {
                rssi = rssi_by_id[n];
                break;
            }
        }
        char rssi_num[8];
        const char *rssi_json = "null";
        if (rssi != kRssiInvalid) {
            snprintf(rssi_num, sizeof(rssi_num), "%d", rssi);
            rssi_json = rssi_num;
        }
        wrote = snprintf(out + used, out_len - used,
                         "%s{\"node_id\":%" PRIu64 ",\"endpoint\":%u,"
                         "\"kind\":\"%s\",\"name\":\"%s\",\"state\":\"%s\","
                         "\"battery\":%d,\"temp_c\":%.1f,\"humidity\":%.0f,\"rssi\":%s%s}",
                         i ? "," : "", entry.node_id, entry.endpoint_id,
                         kind_name(entry.kind), name, state, battery, temp_c, humidity, rssi_json, buttons);
        if (wrote < 0 || static_cast<size_t>(wrote) >= out_len - used) {
            unlock_registry();
            return ESP_ERR_NO_MEM;
        }
        used += static_cast<size_t>(wrote);
    }
    unlock_registry();
    if (used + 2 > out_len) {
        return ESP_ERR_NO_MEM;
    }
    out[used++] = ']';
    out[used] = 0;
    return ESP_OK;
}

static bool kind_has_onoff(gateway_kind_t kind)
{
    switch (kind) {
    case GW_KIND_PLUG:
    case GW_KIND_LIGHT:
    case GW_KIND_LIGHT_CT:
    case GW_KIND_LIGHT_COLOR:
    case GW_KIND_ONOFF:
    case GW_KIND_FAN:
    case GW_KIND_SWITCH:
        return true;
    default:
        return false;
    }
}

int gateway_nodes_write_peer_devices(char *buf, size_t len)
{
    if (!buf || len < 3) {
        return -1;
    }
    size_t used = 0;
    int wrote = snprintf(buf, len, "[");
    if (wrote < 0 || (size_t)wrote >= len) {
        return -1;
    }
    used = (size_t)wrote;
    bool first = true;
    int count = gateway_nodes_count();
    for (int i = 0; i < count && used + 180 < len; i++) {
        gateway_node_view_t view;
        if (gateway_nodes_copy(i, &view) != ESP_OK) {
            continue;
        }
        bool contact = view.kind == GW_KIND_CONTACT;
        bool temp = view.kind == GW_KIND_CLIMATE || view.kind == GW_KIND_THERMOSTAT;
        bool onoff = kind_has_onoff(view.kind);
        if (!contact && !temp && !onoff && view.kind != GW_KIND_MOTION) {
            continue;
        }
        char name[48];
        size_t k = 0;
        for (const char *p = view.name; *p && k + 1 < sizeof(name); p++) {
            name[k++] = (*p == '"' || *p == '\\') ? '\'' : *p;
        }
        name[k] = 0;
        wrote = snprintf(buf + used, len - used,
                         "%s{\"id\":\"%llu\",\"name\":\"%s\",\"kind\":\"%s\","
                         "\"has_temp\":%s,\"temp_c\":%.2f,\"has_onoff\":%s,\"on\":%s,"
                         "\"has_contact\":%s,\"contact_open\":%s}",
                         first ? "" : ",", (unsigned long long)view.node_id, name,
                         kind_name(static_cast<sensor_kind_t>(view.kind)), temp ? "true" : "false",
                         (double)view.temp_c, onoff ? "true" : "false", view.on ? "true" : "false",
                         contact ? "true" : "false", view.contact_open ? "true" : "false");
        if (wrote < 0 || (size_t)wrote >= len - used) {
            break;
        }
        used += (size_t)wrote;
        first = false;
    }
    if (used + 2 > len) {
        return -1;
    }
    buf[used++] = ']';
    buf[used] = 0;
    return (int)used;
}

esp_err_t gateway_nodes_peer_set_on(const char *id, bool on)
{
    if (!id || !id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    char *end = nullptr;
    unsigned long long node = strtoull(id, &end, 10);
    if (!end || *end || node == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return gateway_nodes_set_on(node, on);
}

esp_err_t gateway_nodes_register_console(void)
{
    ESP_RETURN_ON_ERROR(gateway_nodes_init(), TAG, "Cannot load node registry");
    static const esp_matter::console::command_t command = {
        .name = "gateway",
        .description = "Standalone gateway sensor registry. Run: matter esp gateway help",
        .handler = command_dispatch,
    };
    return esp_matter::console::add_commands(&command, 1);
}
