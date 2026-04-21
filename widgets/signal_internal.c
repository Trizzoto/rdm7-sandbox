/**
 * signal_internal.c — WASM stub for internal signal injection.
 * The real implementation reads ESP32 metrics (CPU, heap, GPIO, temp sensor).
 * In WASM, none of these exist, so all functions are no-ops.
 */
#include "signal_internal.h"
#include "signal.h"

static fuel_cal_config_t s_fuel_cal = {
    .empty_v    = 0.5f,
    .full_v     = 3.0f,
    .full_value = 100.0f,
    .enabled    = false,
};

void signal_internal_start(void) { /* no-op in WASM */ }
void signal_internal_stop(void) { /* no-op in WASM */ }

void signal_internal_set_fuel_cal(float empty_v, float full_v,
                                  float full_value, bool enabled) {
    s_fuel_cal.empty_v    = empty_v;
    s_fuel_cal.full_v     = full_v;
    s_fuel_cal.full_value = full_value;
    s_fuel_cal.enabled    = enabled;
}

void signal_internal_get_fuel_cal(fuel_cal_config_t *out) {
    if (out) *out = s_fuel_cal;
}

float signal_internal_get_fuel_voltage(void) { return 0.0f; }
void signal_internal_count_frame(void) { /* no-op */ }
