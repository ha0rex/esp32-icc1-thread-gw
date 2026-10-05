#pragma once

#include "sdkconfig.h"

#if !CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONER_DISCOVERY 1
#endif

/* Two sensors plus room for additional local Matter devices. */
#define CHIP_CONFIG_CONTROLLER_MAX_ACTIVE_DEVICES 8

#endif

/* Stock lookup waits 45s and, with one subscribe at a time, that delay
 * stacks across every device. A Thread operational answer often arrives
 * after 8s, so keep the wait long enough to hear it and short enough
 * that a missing device does not block the next one. */
#define CHIP_CONFIG_ADDRESS_RESOLVE_MAX_LOOKUP_TIME_MS 20000
