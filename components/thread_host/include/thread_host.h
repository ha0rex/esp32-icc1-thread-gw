#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "spinel_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THREAD_NET_DOWN = 0,
    THREAD_NET_ATTACHING,
    THREAD_NET_ATTACHED,
    THREAD_NET_LEADER,
} thread_net_state_t;

typedef struct {
    spinel_host_status_t rcp;
    thread_net_state_t net;
    uint8_t channel;     /**< 11–26 when known; 0 if unknown */
    uint16_t pan_id;
    char network_name[32];
    uint8_t device_count; /**< Matter/Thread endpoints tracked in NVS (scaffold) */
    char role_str[16];
} thread_host_status_t;

esp_err_t thread_host_start(void);
void thread_host_get_status(thread_host_status_t *out);

/** Soft helpers for portal (full OT dataset APIs land with OTBR integration). */
esp_err_t thread_host_request_form(uint8_t channel);
esp_err_t thread_host_request_leave(void);

#ifdef __cplusplus
}
#endif
