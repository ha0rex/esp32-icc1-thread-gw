#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Register `matter esp gateway ...` commands. */
esp_err_t gateway_nodes_register_console(void);

/** Load the persistent node registry. Safe to call more than once. */
esp_err_t gateway_nodes_init(void);

/** Start the first subscription. Further devices are started by retry_watch. */
void gateway_nodes_refresh(void);

/** Subscribe the next device that is not already listening. Two may run at once. */
void gateway_nodes_retry_watch(void);

/** Read current attributes again. Safe to call on a timer. */
void gateway_nodes_poll(void);

/** Read the contact value for sensors that do not push a magnet change. */
void gateway_nodes_poll_contacts(void);

/** Lowest unused node id, starting at 1234. */
uint64_t gateway_nodes_next_id(void);

/** Matter device kinds. Values 1 and 2 match older registries. Any Thread vendor. */
typedef enum {
    GW_KIND_CONTACT = 1,
    GW_KIND_MOTION = 2,
    GW_KIND_LEAK = 3,
    GW_KIND_CLIMATE = 4,
    GW_KIND_AIR = 5,
    GW_KIND_PLUG = 6,
    GW_KIND_LIGHT = 7,
    GW_KIND_LIGHT_CT = 8,
    GW_KIND_LIGHT_COLOR = 9,
    GW_KIND_BUTTON = 10,
    GW_KIND_SHADE = 11,
    GW_KIND_ONOFF = 12,
    GW_KIND_OCCUPANCY = 13,
    GW_KIND_LUX = 14,
    GW_KIND_LOCK = 15,
    GW_KIND_THERMOSTAT = 16,
    GW_KIND_FAN = 17,
    GW_KIND_SMOKE = 18,
    GW_KIND_SWITCH = 19,
} gateway_kind_t;

typedef struct {
    uint64_t node_id;
    uint16_t endpoint_id;
    gateway_kind_t kind;
    char name[24];
    char state[64];
    bool on;
    uint8_t level_pct;
    uint16_t mireds;
    float hue_deg;
    float saturation_pct;
    bool contact_open;
    bool motion;
    bool leak;
    float temp_c;
    float humidity_pct;
    float co2_ppm;
    float pm25;
    float power_w;
    uint8_t shade_open_pct;
    uint8_t battery_pct;
    uint32_t button_presses;
    uint8_t button_event;
    uint8_t button_count;
    uint8_t button_mode[6];
    uint8_t button_on[6];
    uint16_t button_ep[6];
    uint32_t button_press_n[6];
    uint8_t button_ev[6];
    char button_name[6][24];
    float lux;
    bool lock_secured;
    uint8_t thermo_mode;
    float heat_set_c;
    bool smoke;
    bool co_alarm;
    uint32_t valid;
} gateway_node_view_t;

#define GW_VALID_ON        (1u << 0)
#define GW_VALID_LEVEL     (1u << 1)
#define GW_VALID_MIREDS    (1u << 2)
#define GW_VALID_COLOR     (1u << 3)
#define GW_VALID_CONTACT   (1u << 4)
#define GW_VALID_MOTION    (1u << 5)
#define GW_VALID_LEAK      (1u << 6)
#define GW_VALID_TEMP      (1u << 7)
#define GW_VALID_HUMIDITY  (1u << 8)
#define GW_VALID_CO2       (1u << 9)
#define GW_VALID_PM25      (1u << 10)
#define GW_VALID_POWER     (1u << 11)
#define GW_VALID_SHADE     (1u << 12)
#define GW_VALID_BATTERY   (1u << 13)
#define GW_VALID_LUX       (1u << 14)
#define GW_VALID_LOCK      (1u << 15)
#define GW_VALID_THERMO    (1u << 16)
#define GW_VALID_SMOKE     (1u << 17)
#define GW_VALID_CO        (1u << 18)

/** Save a device and subscribe to the attributes for its kind. */
esp_err_t gateway_nodes_remember(uint64_t node_id, uint16_t endpoint_id,
                                 const char *kind, const char *name);

int gateway_nodes_count(void);
esp_err_t gateway_nodes_copy(int index, gateway_node_view_t *out);
esp_err_t gateway_nodes_copy_id(uint64_t node_id, gateway_node_view_t *out);

/** Commands sent when Home changes a light, plug, or shade. */
esp_err_t gateway_nodes_set_on(uint64_t node_id, bool on);
esp_err_t gateway_nodes_set_level_pct(uint64_t node_id, int percent);
esp_err_t gateway_nodes_set_hue_sat(uint64_t node_id, float hue_deg, float saturation_pct);
esp_err_t gateway_nodes_set_mireds(uint64_t node_id, int mireds);
esp_err_t gateway_nodes_set_shade_open_pct(uint64_t node_id, int open_pct);
esp_err_t gateway_nodes_set_lock(uint64_t node_id, bool secured);
esp_err_t gateway_nodes_set_thermo_mode(uint64_t node_id, int hap_mode);
esp_err_t gateway_nodes_set_heat_setpoint(uint64_t node_id, float celsius);
esp_err_t gateway_nodes_set_fan(uint64_t node_id, bool on);

/** Drop the local record and subscription. The sensor stays on the fabric. */
esp_err_t gateway_nodes_forget(uint64_t node_id);

/** Change the stored name and type. A type change rebuilds the HomeKit accessory. */
esp_err_t gateway_nodes_update(uint64_t node_id, const char *name, const char *kind);

/** One remote button as edited from the device details. */
typedef struct {
    char name[24];
    uint8_t mode;
} gateway_button_edit_t;

/** Name each button and choose stateless (press) or stateful (on/off toggle). */
esp_err_t gateway_nodes_set_buttons(uint64_t node_id, const gateway_button_edit_t *buttons,
                                    uint8_t count);

/** Home turned a stateful button switch on or off. */
esp_err_t gateway_nodes_set_button_on(uint64_t node_id, uint8_t index, bool on);

/** Ask one node for its current attributes. */
esp_err_t gateway_nodes_read_now(uint64_t node_id);

/**
 * Append a JSON array of registered sensors, including the latest report.
 * The text starts with '['.
 */
esp_err_t gateway_nodes_write_json(char *out, size_t out_len);

/** JSON array of devices another gateway on the LAN may read or switch. */
int gateway_nodes_write_peer_devices(char *buf, size_t len);

/** Turn a local on/off device on or off. id is the decimal node id. */
esp_err_t gateway_nodes_peer_set_on(const char *id, bool on);
