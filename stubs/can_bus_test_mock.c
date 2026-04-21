/* can_bus_test_mock.c -- canned CAN bus scanner for the wizard's Step 4.
 * Signatures must match firmware_src/can/can_bus_test.h exactly.
 */
#include "esp_idf_shim.h"
#include "can_bus_test.h"
#include "can_manager.h"
#include <emscripten.h>
#include <string.h>

static can_scan_report_t s_report;
static can_scan_ui_cb_t  s_cb = NULL;
static bool              s_running = false;
static double            s_started_ms = 0;
static int               s_phase = 0;  /* 0..3 bitrates, 4 restoring, 5 complete */

/* Bitrate labels are referenced in the wizard UI (via BR_NAMES at call
 * site). We just report frame counts per slot so the wizard thinks the
 * probe succeeded on slot 2 (500 kbps by convention). */

static void _fire_cb(void) {
    if (s_cb) s_cb();
}

bool can_bus_test_start(void) {
    if (s_running) return false;
    memset(&s_report, 0, sizeof(s_report));
    s_report.state = CAN_SCAN_STOPPING;
    s_report.recommended_bitrate = -1;
    s_running = true;
    s_started_ms = emscripten_get_now();
    s_phase = 0;
    _fire_cb();
    return true;
}

void can_bus_test_cancel(void) {
    if (!s_running) return;
    s_running = false;
    s_report.state = CAN_SCAN_CANCELLED;
    _fire_cb();
}

bool can_bus_test_is_running(void) { return s_running; }

/* Each tick we check elapsed time and advance the mock state machine.
 * Total scan runs ~3 s, producing a "traffic detected at 500 kbps" result
 * so the wizard can show a recommended bitrate and proceed. */
static void _tick(void) {
    if (!s_running) return;
    double elapsed = emscripten_get_now() - s_started_ms;

    if (elapsed < 250) {
        s_report.state = CAN_SCAN_STOPPING;
    } else if (elapsed < 3000) {
        s_report.state = CAN_SCAN_TESTING_BITRATE;
        int idx = (int)((elapsed - 250) / 700);   /* 0..3 across ~2.8 s */
        if (idx > 3) idx = 3;
        s_report.current_bitrate_idx = (uint8_t)idx;
        for (int i = 0; i < idx; i++) {
            s_report.results[i].bitrate_index = (uint8_t)i;
            s_report.results[i].frames_received   = (i == 2) ? 42 : 0;
            s_report.results[i].traffic_detected  = (i == 2);
            s_report.results[i].unique_id_count   = (i == 2) ? 5 : 0;
        }
    } else if (elapsed < 3300) {
        s_report.state = CAN_SCAN_RESTORING;
    } else {
        /* Final state - slot 2 (500 kbps) won */
        s_report.state = CAN_SCAN_COMPLETE;
        s_report.recommended_bitrate = 2;
        s_report.results[2].traffic_detected = true;
        s_report.results[2].frames_received  = 142;
        s_report.results[2].unique_id_count  = 8;
        s_running = false;
    }
    _fire_cb();
}

can_scan_state_t can_bus_test_get_state(void)         { _tick(); return s_report.state; }
uint8_t can_bus_test_get_current_bitrate(void)        { _tick(); return s_report.current_bitrate_idx; }
const can_scan_report_t *can_bus_test_get_report(void){ _tick(); return &s_report; }
void can_bus_test_set_ui_callback(can_scan_ui_cb_t cb){ s_cb = cb; }

/* ── can_manager surface (minimal) ───────────────────────────────────── */
static uint32_t s_bitrate_kbps = 500;
esp_err_t can_manager_set_bitrate(uint32_t kbps) { s_bitrate_kbps = kbps; return ESP_OK; }
uint32_t  can_manager_get_bitrate(void)          { return s_bitrate_kbps; }
esp_err_t can_manager_start(void)                { return ESP_OK; }
esp_err_t can_manager_stop(void)                 { return ESP_OK; }
esp_err_t can_manager_suspend(void)              { return ESP_OK; }
esp_err_t can_manager_resume(void)               { return ESP_OK; }
