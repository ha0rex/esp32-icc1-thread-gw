#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool fabric_commissioned;
    uint16_t endpoint_count;
    char discriminator[8];
    char setup_pin[12]; /**< Matter manual pairing code placeholder */
    char vendor_name[32];
    char product_name[40];
} matter_bridge_status_t;

esp_err_t matter_bridge_start(void);
void matter_bridge_get_status(matter_bridge_status_t *out);

#ifdef __cplusplus
}
#endif
