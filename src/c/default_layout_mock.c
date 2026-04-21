/* default_layout_mock.c -- builds the firmware's canonical default
 * layout as a cJSON root, in-memory, for the sandbox dashboard scene.
 *
 * Mirrors RDM-7_Dash/main/layout/default_layout.c widget-for-widget
 * so that layout_manager_apply_json() renders the exact layout a user
 * would see on a fresh device with Haltech Nexus signals bound.
 * Unlike the firmware version, this does NOT write to LittleFS and
 * does NOT own the cJSON root — callers must cJSON_Delete it.
 */

#include "cJSON.h"
#include "layout_manager.h"
#include <stdio.h>
#include <stdint.h>

#ifndef SCREEN_W
#define SCREEN_W 800
#endif
#ifndef SCREEN_H
#define SCREEN_H 480
#endif

/* Panel box positions — must match box_positions[8][2] in widget_panel.c. */
static const struct { int16_t x, y; } s_panel_pos[8] = {
    {-312, -26}, {-146, -26}, {-312, 82}, {-146, 82},
    { 146, -26}, { 312, -26}, { 146, 82}, { 312, 82},
};

/* Warning circle positions — must match widget_warning.c. */
static const struct { int16_t x, y; } s_warn_pos[8] = {
    {-352, -148}, {-292, -148}, {-232, -148}, {-172, -148},
    { 172, -148}, { 232, -148}, { 292, -148}, { 352, -148},
};

static void _add_widget(cJSON *arr, const char *type_str, const char *id,
                        int16_t x, int16_t y, uint16_t w, uint16_t h,
                        cJSON *config) {
    cJSON *wj = cJSON_CreateObject();
    if (!wj) return;
    cJSON_AddStringToObject(wj, "type", type_str);
    cJSON_AddStringToObject(wj, "id", id);
    cJSON_AddNumberToObject(wj, "x", x);
    cJSON_AddNumberToObject(wj, "y", y);
    cJSON_AddNumberToObject(wj, "w", w);
    cJSON_AddNumberToObject(wj, "h", h);
    if (config) cJSON_AddItemToObject(wj, "config", config);
    else        cJSON_AddObjectToObject(wj, "config");
    cJSON_AddItemToArray(arr, wj);
}

cJSON *dashboard_build_default_layout_root(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "schema_version", LAYOUT_SCHEMA_VERSION);
    cJSON_AddStringToObject(root, "name", "default");
    cJSON_AddNumberToObject(root, "screen_w", SCREEN_W);
    cJSON_AddNumberToObject(root, "screen_h", SCREEN_H);
    cJSON_AddStringToObject(root, "ecu", "Haltech");
    cJSON_AddStringToObject(root, "ecu_version", "Nexus");

    cJSON *arr = cJSON_AddArrayToObject(root, "widgets");

    /* Divider under RPM bar */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg, "bg_color",     10597);
        cJSON_AddNumberToObject(cfg, "bg_opa",       255);
        cJSON_AddNumberToObject(cfg, "border_color", 10597);
        cJSON_AddNumberToObject(cfg, "border_width", 0);
        cJSON_AddNumberToObject(cfg, "border_radius",0);
        cJSON_AddNumberToObject(cfg, "shadow_width", 0);
        _add_widget(arr, "shape_panel", "shape_panel_1", 0, -182, 800, 9, cfg);
    }

    /* RPM bar — driven by the RPM signal. */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg, "rpm_max", 7000);
        cJSON_AddNumberToObject(cfg, "redline", 6500);
        cJSON_AddStringToObject(cfg, "signal_name", "RPM");
        _add_widget(arr, "rpm_bar", "rpm_bar_0", 0, -215, 800, 55, cfg);
    }

    /* 8 panels in 2 rows × 4 columns. Decimals set per-signal so
     * LAMBDA shows "0.920" not "0" and FUEL_TRIM shows "1.5" not "1". */
    {
        static const struct { const char *label, *signal; int decimals; } panel_cfg[8] = {
            { "IGNITION",  "IGNITION",        1 },
            { "MAP",       "MAP",             1 },
            { "THROTTLE",  "THROTTLE",        0 },
            { "COOLANT",   "COOLANT_TEMP",    1 },
            { "INTAKE",    "INTAKE_AIR_TEMP", 0 },
            { "LAMBDA",    "LAMBDA",          3 },
            { "OIL TEMP",  "OIL_TEMP",        1 },
            { "FUEL TRIM", "FUEL_TRIM",       1 },
        };
        for (int i = 0; i < 8; i++) {
            char id[16];
            snprintf(id, sizeof(id), "panel_%d", i);
            cJSON *cfg = cJSON_CreateObject();
            cJSON_AddNumberToObject(cfg, "slot",     i);
            cJSON_AddStringToObject(cfg, "label",       panel_cfg[i].label);
            cJSON_AddStringToObject(cfg, "signal_name", panel_cfg[i].signal);
            cJSON_AddNumberToObject(cfg, "decimals",    panel_cfg[i].decimals);
            _add_widget(arr, "panel", id,
                        s_panel_pos[i].x, s_panel_pos[i].y, 155, 92, cfg);
        }
    }

    /* Bottom bars: coolant (left, blue→red gradient), throttle (right). */
    {
        cJSON *cfg0 = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg0, "slot", 0);
        cJSON_AddStringToObject(cfg0, "label", "COOLANT");
        cJSON_AddStringToObject(cfg0, "signal_name", "COOLANT_TEMP");
        cJSON_AddNumberToObject(cfg0, "bar_low", 0);
        cJSON_AddNumberToObject(cfg0, "bar_high", 120);
        cJSON_AddNumberToObject(cfg0, "bar_low_color",  31);
        cJSON_AddNumberToObject(cfg0, "bar_high_color", 63488);
        _add_widget(arr, "bar", "bar_0", -240, 209, 300, 30, cfg0);

        cJSON *cfg1 = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg1, "slot", 1);
        cJSON_AddStringToObject(cfg1, "label", "THROTTLE");
        cJSON_AddStringToObject(cfg1, "signal_name", "THROTTLE");
        cJSON_AddNumberToObject(cfg1, "bar_low", 0);
        cJSON_AddNumberToObject(cfg1, "bar_high", 100);
        cJSON_AddNumberToObject(cfg1, "bar_low_color",  31);
        cJSON_AddNumberToObject(cfg1, "bar_high_color", 63488);
        _add_widget(arr, "bar", "bar_1", 240, 209, 300, 30, cfg1);
    }

    /* Gear panel — centre-bottom, uses the GEAR signal. */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg, "slot", 8);
        cJSON_AddStringToObject(cfg, "label", "GEAR");
        cJSON_AddStringToObject(cfg, "signal_name", "GEAR");
        cJSON_AddNumberToObject(cfg, "bg_color",     14823);
        cJSON_AddNumberToObject(cfg, "border_color", 14823);
        cJSON_AddNumberToObject(cfg, "decimals", 0);
        _add_widget(arr, "panel", "panel_gear", 0, 178, 92, 92, cfg);
    }

    /* L / R turn indicators. */
    {
        cJSON *cfgL = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfgL, "slot", 0);
        cJSON_AddNumberToObject(cfgL, "opa_off", 180);
        _add_widget(arr, "indicator", "indicator_0", -95, -133, 35, 35, cfgL);

        cJSON *cfgR = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfgR, "slot", 1);
        cJSON_AddNumberToObject(cfgR, "opa_off", 180);
        _add_widget(arr, "indicator", "indicator_1", 95, -133, 35, 35, cfgR);
    }

    /* 8 warning slots across the top. */
    for (int i = 0; i < 8; i++) {
        char id[16];
        snprintf(id, sizeof(id), "warning_%d", i);
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddNumberToObject(cfg, "slot", i);
        cJSON_AddNumberToObject(cfg, "inactive_opa", 180);
        _add_widget(arr, "warning", id,
                    s_warn_pos[i].x, s_warn_pos[i].y, 20, 20, cfg);
    }

    /* Vehicle speed (large centre text). */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddStringToObject(cfg, "static_text", "---");
        cJSON_AddStringToObject(cfg, "signal_name", "VEHICLE_SPEED");
        cJSON_AddNumberToObject(cfg, "decimals", 0);
        cJSON_AddNumberToObject(cfg, "rotation", 0);
        cJSON_AddStringToObject(cfg, "font", "fugaz_56");
        cJSON_AddNumberToObject(cfg, "text_color", 65535);
        _add_widget(arr, "text", "text_1", 0, 80, 120, 60, cfg);
    }

    /* RDM logo. */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddStringToObject(cfg, "image_name", "RDM");
        cJSON_AddNumberToObject(cfg, "image_scale", 256);
        cJSON_AddNumberToObject(cfg, "opacity", 255);
        _add_widget(arr, "image", "image_1", 0, -60, 120, 62, cfg);
    }

    /* RPM numeric readout. */
    {
        cJSON *cfg = cJSON_CreateObject();
        cJSON_AddStringToObject(cfg, "static_text", "---");
        cJSON_AddStringToObject(cfg, "signal_name", "RPM");
        cJSON_AddNumberToObject(cfg, "decimals", 0);
        cJSON_AddNumberToObject(cfg, "rotation", 0);
        cJSON_AddStringToObject(cfg, "font", "fugaz_28");
        cJSON_AddNumberToObject(cfg, "text_color", 65535);
        _add_widget(arr, "text", "text_2", 0, -133, 120, 30, cfg);
    }

    /* Signals — stubs only, with plausible decode parameters. Real
     * CAN decode doesn't run in the sandbox; signal values come via
     * signal_inject_test_value() from the dashboard tick loop. The
     * entries need to exist so widgets can resolve signal_name →
     * registry index. */
    cJSON *sigs = cJSON_AddArrayToObject(root, "signals");
    static const char *SIG_NAMES[] = {
        "RPM", "MAP", "THROTTLE", "COOLANT_TEMP", "INTAKE_AIR_TEMP",
        "LAMBDA", "OIL_TEMP", "OIL_PRESSURE", "FUEL_PRESSURE",
        "IGNITION", "VEHICLE_SPEED", "GEAR", "BATTERY_VOLTAGE",
        "FUEL_TRIM",
    };
    for (size_t i = 0; i < sizeof(SIG_NAMES) / sizeof(SIG_NAMES[0]); i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", SIG_NAMES[i]);
        cJSON_AddNumberToObject(s, "can_id",     0);
        cJSON_AddNumberToObject(s, "bit_start",  0);
        cJSON_AddNumberToObject(s, "bit_length", 16);
        cJSON_AddNumberToObject(s, "scale",      1.0);
        cJSON_AddNumberToObject(s, "offset",     0);
        cJSON_AddNumberToObject(s, "is_signed",  0);
        cJSON_AddNumberToObject(s, "endian",     0);
        cJSON_AddItemToArray(sigs, s);
    }

    return root;
}
