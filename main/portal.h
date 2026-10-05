#pragma once

#include "esp_err.h"

/** Start the browser portal on port 80. Safe to call after Matter is up. */
esp_err_t portal_start(void);
