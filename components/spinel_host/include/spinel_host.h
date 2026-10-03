#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPINEL_LINK_UNKNOWN = 0,
    SPINEL_LINK_NO_UART,
    SPINEL_LINK_NO_RESPONSE,
    SPINEL_LINK_OK,
} spinel_link_status_t;

typedef struct {
    spinel_link_status_t link;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t probes_ok;
    uint32_t probes_fail;
    char ncp_version[96]; /**< Spinel PROP_NCP_VERSION string if known */
} spinel_host_status_t;

esp_err_t spinel_host_start(void);
void spinel_host_get_status(spinel_host_status_t *out);
/** Send Spinel CMD_PROP_VALUE_GET for PROP_NCP_VERSION (HDLC framed). */
esp_err_t spinel_host_probe_once(void);

#ifdef __cplusplus
}
#endif
