/* device_settings.h stub -- only the types config_store.h pulls in. */
#pragma once
#include "esp_idf_shim.h"

/* Brightness dimmer config referenced by config_store.h. Minimal shape:
 * a few floats and ints; exact field names don't matter for the sandbox
 * build — nothing reads them. */
typedef struct {
    uint8_t  manual_brightness;
    bool     auto_dim;
    uint8_t  min_brightness;
    uint8_t  max_brightness;
} brightness_dimmer_config_t;

/* Stub no-op functions that device_settings exposes; real UI module not
 * built in the sandbox. */
static inline void device_settings_open(void) {}
static inline void device_settings_close(void) {}
