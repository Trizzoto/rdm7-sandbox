#pragma once
#include "esp_idf_shim.h"
/* Minimal TWAI types for files that include this header. Real API is
 * stubbed via can_bus_test_mock.c's can_manager_* symbols. */
typedef struct { uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; } twai_message_t;
