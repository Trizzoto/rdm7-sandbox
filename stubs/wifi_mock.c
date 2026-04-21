/* wifi_mock.c -- canned wifi_manager implementation for the sandbox.
 * Signatures must match firmware_src/net/wifi_manager.h exactly.
 */
#include "esp_idf_shim.h"
#include "wifi_manager.h"
#include <emscripten.h>
#include <string.h>

#define MOCK_SCAN_COUNT 6
static const wifi_mgr_ap_record_t s_mock_scan[MOCK_SCAN_COUNT] = {
    { "Home_2.4",      -42, 3 },
    { "CarWiFi",       -55, 3 },
    { "iPhone_Tommy",  -58, 3 },
    { "TeslaGuest",    -71, 0 },
    { "Neighbor5G",    -78, 3 },
    { "RDM7-7B0D",     -35, 3 },
};

static wifi_mgr_state_t       s_state = WIFI_MGR_STATE_OFF;
static bool                   s_started = false;
static bool                   s_ap_enabled = true;
static double                 s_connect_started_ms = 0;
static wifi_mgr_event_cb_t    s_cb = NULL;
static void                  *s_cb_user = NULL;
static char                   s_connected_ssid[33] = "";

static void _set_state(wifi_mgr_state_t next) {
    if (next == s_state) return;
    s_state = next;
    if (s_cb) s_cb(next, s_cb_user);
}

/* Lifecycle */
void wifi_manager_init(void)       { _set_state(WIFI_MGR_STATE_IDLE); }
void wifi_manager_start(void)      { s_started = true; _set_state(s_ap_enabled ? WIFI_MGR_STATE_AP_ONLY : WIFI_MGR_STATE_IDLE); }
void wifi_manager_stop(void)       { s_started = false; _set_state(WIFI_MGR_STATE_OFF); }
bool wifi_manager_is_started(void) { return s_started; }

/* STA */
void wifi_manager_scan(void) {
    _set_state(WIFI_MGR_STATE_SCANNING);
    /* Real firmware takes ~1.5 s to scan; fake an async completion. */
    _set_state(WIFI_MGR_STATE_IDLE);
}
void wifi_manager_connect(const char *ssid, const char *password) {
    (void)password;
    if (ssid) strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
    _set_state(WIFI_MGR_STATE_CONNECTING);
    s_connect_started_ms = emscripten_get_now();
}
void wifi_manager_disconnect(void) { _set_state(WIFI_MGR_STATE_IDLE); s_connected_ssid[0] = 0; }
void wifi_manager_forget(void)     { wifi_manager_disconnect(); }
void wifi_manager_auto_connect(void) { /* no saved creds in mock */ }

/* AP */
void wifi_manager_enable_ap(bool enable)           { s_ap_enabled = enable; }
bool wifi_manager_is_ap_enabled(void)              { return s_ap_enabled; }
void wifi_manager_set_ap_password(const char *pw)  { (void)pw; }

/* State queries */
wifi_mgr_state_t wifi_manager_get_state(void)          { return s_state; }
const char *wifi_manager_get_connected_ssid(void)      { return s_connected_ssid[0] ? s_connected_ssid : NULL; }
const char *wifi_manager_get_ap_ssid(void)             { return "RDM7-7B0D"; }
const char *wifi_manager_get_sta_ip(void)              { return s_state == WIFI_MGR_STATE_CONNECTED ? "192.168.1.42" : NULL; }
const char *wifi_manager_get_ap_ip(void)               { return "192.168.4.1"; }

/* Scan results */
uint16_t wifi_manager_get_scan_results(wifi_mgr_ap_record_t *out, uint16_t max) {
    if (!out || max == 0) return 0;
    uint16_t n = max < MOCK_SCAN_COUNT ? max : MOCK_SCAN_COUNT;
    for (uint16_t i = 0; i < n; i++) out[i] = s_mock_scan[i];
    return n;
}

/* Event subscription */
void wifi_manager_set_event_cb(wifi_mgr_event_cb_t cb, void *user_data) {
    s_cb = cb;
    s_cb_user = user_data;
}

/* Called from main_sandbox.c's frame loop to advance mock state. */
void wifi_manager_tick(void) {
    if (s_state == WIFI_MGR_STATE_CONNECTING
        && emscripten_get_now() - s_connect_started_ms > 1500) {
        _set_state(WIFI_MGR_STATE_CONNECTED);
    }
}
