#pragma once

#include <stdint.h>

#include "esp_err.h"

/** Start the HomeKit bridge after Wi-Fi is up. Safe to call once from app_main. */
esp_err_t homekit_bridge_start(void);

/** Add or refresh the HomeKit accessory for a paired Matter node. */
void homekit_bridge_attach(uint64_t node_id);

/** Push the latest Matter report into HomeKit. */
void homekit_bridge_on_node(uint64_t node_id);

/** Remove the HomeKit accessory. The Matter device stays on the fabric. */
void homekit_bridge_detach(uint64_t node_id);

/** Update the accessory name shown in Home. */
void homekit_bridge_rename(uint64_t node_id, const char *name);

/**
 * Drop every Home controller so the bridge can be added again.
 * A paired bridge reboots after the erase. An unpaired bridge just
 * reopens the pairing window. Wi-Fi, Thread, and Matter nodes stay.
 */
esp_err_t homekit_bridge_reset_pairings(void);

typedef struct {
    bool started;
    bool paired;
    uint16_t accessory_count;
    char setup_code[16];
    char setup_id[8];
    char setup_uri[40];
    char status[48];
} homekit_bridge_status_t;

void homekit_bridge_get_status(homekit_bridge_status_t *out);
