/* ecu_presets_mock.c — canned ECU preset list for Step 3 of the wizard.
 *
 * The real ecu_presets.c embeds a big static table of make/model/version
 * tuples plus the DBC mapping for each. For the tutorial we only need
 * the picker UI to have plausible entries to scroll through and select.
 */
#include "esp_idf_shim.h"
#include <string.h>

typedef struct {
    const char *make;
    const char *version;
    const char *display;  /* "Make — Version" shown in the picker */
} ecu_preset_t;

static const ecu_preset_t s_presets[] = {
    { "Haltech",    "Elite 1500/2000/2500",  "Haltech — Elite 1500/2000/2500" },
    { "Haltech",    "Nexus R5",              "Haltech — Nexus R5" },
    { "Link",       "G4+ / G4X",             "Link — G4+ / G4X" },
    { "MoTeC",      "M1",                    "MoTeC — M1 Series" },
    { "MaxxECU",    "Mini / Street / Sport", "MaxxECU — Mini / Street / Sport" },
    { "MaxxECU",    "V1.2",                  "MaxxECU — V1.2" },
    { "MS3-Pro",    "PnP",                   "MS3-Pro — PnP" },
    { "Ford",       "BA / BF",               "Ford Falcon — BA / BF" },
    { "Ford",       "FG",                    "Ford Falcon — FG" },
    { "Custom",     "No preset",             "No preset — configure manually" },
};
#define PRESET_COUNT (sizeof(s_presets) / sizeof(s_presets[0]))

int ecu_presets_count(void) { return (int)PRESET_COUNT; }

const char *ecu_presets_get_display(int index) {
    if (index < 0 || index >= (int)PRESET_COUNT) return NULL;
    return s_presets[index].display;
}

const char *ecu_presets_get_make(int index) {
    if (index < 0 || index >= (int)PRESET_COUNT) return NULL;
    return s_presets[index].make;
}

const char *ecu_presets_get_version(int index) {
    if (index < 0 || index >= (int)PRESET_COUNT) return NULL;
    return s_presets[index].version;
}

/* The firmware stores the selected preset by its "make|version" string
 * in NVS. In the sandbox we just hold it in memory. */
static char s_current[96] = {0};

esp_err_t ecu_presets_set_current(const char *make, const char *version) {
    if (!make || !version) { s_current[0] = '\0'; return ESP_OK; }
    snprintf(s_current, sizeof(s_current), "%s|%s", make, version);
    return ESP_OK;
}

const char *ecu_presets_get_current(void) { return s_current[0] ? s_current : NULL; }

esp_err_t ecu_presets_apply(int index) {
    if (index < 0 || index >= (int)PRESET_COUNT) return ESP_ERR_NOT_FOUND;
    ecu_presets_set_current(s_presets[index].make, s_presets[index].version);
    ESP_LOGI("ecu_mock", "Applied preset %d: %s", index, s_presets[index].display);
    return ESP_OK;
}
