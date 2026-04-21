/* esp_stubs.h — wasm-editor compat shim. The ported widget + layout
 * code includes "esp_stubs.h" (the name it used there). We forward to
 * our canonical esp_idf_shim.h so both spellings work. */
#pragma once
#include "esp_idf_shim.h"
