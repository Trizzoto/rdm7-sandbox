/* config_store_mock.c — in-memory NVS replacement for the sandbox.
 *
 * The firmware uses config_store_* calls to persist settings into NVS
 * flash. In the browser we just hold values in JS-accessible memory —
 * Phase 2 may back these with localStorage so tutorial progress
 * survives a page reload.
 */
#include "esp_idf_shim.h"
#include <string.h>

static bool s_first_run_done = false;

esp_err_t config_store_init(void) { return ESP_OK; }

esp_err_t config_store_save_first_run_done(bool done) {
    s_first_run_done = done;
    ESP_LOGI("cfg_mock", "first_run_done = %d", done);
    return ESP_OK;
}
esp_err_t config_store_load_first_run_done(bool *done) {
    if (!done) return ESP_ERR_INVALID_ARG;
    *done = s_first_run_done;
    return ESP_OK;
}

/* Brightness/rotation/log-rate are read by device_settings in Phase 2.
 * Return plausible defaults so callers don't explode. */
esp_err_t config_store_load_brightness(uint8_t *pct)     { if (pct) *pct = 80;  return ESP_OK; }
esp_err_t config_store_save_brightness(uint8_t pct)      { (void)pct;            return ESP_OK; }
esp_err_t config_store_load_rotation(uint8_t *rot)       { if (rot) *rot = 0;   return ESP_OK; }
esp_err_t config_store_save_rotation(uint8_t rot)        { (void)rot;            return ESP_OK; }
esp_err_t config_store_load_log_rate_hz(uint16_t *hz)    { if (hz)  *hz  = 50;  return ESP_OK; }
esp_err_t config_store_save_log_rate_hz(uint16_t hz)     { (void)hz;             return ESP_OK; }
