/* wizard_sandbox.c -- faithful clone of the RDM-7 Dash first_run_wizard.
 *
 * Replicates firmware/main/ui/screens/first_run_wizard.c visually and
 * behaviourally. Three-step onboarding:
 *
 *   Step 1: CAN bitrate scan with animated progress (fake data advances
 *           on a timer, simulating the real listen-only probe across
 *           125 k / 250 k / 500 k / 1 M).
 *   Step 2: ECU picker overlay -- two dropdowns (make then version).
 *   Step 3: "Connect Your Device" with WiFi / USB / Hotspot options.
 *           Tapping "Join a WiFi Network" opens a simplified WiFi scan
 *           + fake-connect flow (sandbox only -- real firmware launches
 *           the full ui_wifi.c screen which is 800+ lines).
 *
 * Pure LVGL. Zero ESP-IDF headers. Dimensions, colors, and fonts match
 * theme.h exactly so the sandbox frame visually matches the device. */

#include "lvgl.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ── Theme (copied verbatim from firmware/main/ui/theme.h) ───────────── */
#define THEME_COLOR_BG               lv_color_hex(0x000000)
#define THEME_COLOR_PANEL            lv_color_hex(0x393C39)
#define THEME_COLOR_SECTION_BG       lv_color_hex(0x393C39)
#define THEME_COLOR_BORDER           lv_color_hex(0x181C18)
#define THEME_COLOR_TEXT_PRIMARY     lv_color_hex(0xE8E8E8)
#define THEME_COLOR_TEXT_MUTED       lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_ON_ACCENT   lv_color_hex(0xFFFFFF)
#define THEME_COLOR_ACCENT_BLUE      lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_YELLOW    lv_color_hex(0xF8FC40)
#define THEME_COLOR_STATUS_CONNECTED lv_color_hex(0x2196F3)
#define THEME_COLOR_STATUS_ERROR     lv_color_hex(0xF84040)
#define THEME_FONT_TINY              (&lv_font_montserrat_10)
#define THEME_FONT_SMALL             (&lv_font_montserrat_12)
#define THEME_FONT_LARGE             (&lv_font_montserrat_18)
#define THEME_RADIUS_NORMAL          4

/* ── Layout (from firmware_src/ui/screens/first_run_wizard.c) ────────── */
#define CARD_W   560
#define CARD_H   430
#define BTN_W    500
#define BTN_H     40

/* ── CAN scan mock data ──────────────────────────────────────────────── */
static const char *BR_NAMES[] = {"125 kbps", "250 kbps", "500 kbps", "1 Mbps"};

/* Fake scan result: slot 2 (500 kbps) wins with plausible traffic.
 * Matches real-world output for a modern ECU on the factory harness. */
typedef struct {
    bool     traffic_detected;
    uint32_t frames_received;
    uint8_t  unique_id_count;
} mock_bitrate_result_t;

static const mock_bitrate_result_t MOCK_RESULTS[4] = {
    { false,   0, 0 },   /* 125k */
    { false,   2, 1 },   /* 250k - one spurious frame, not enough */
    { true,  142, 8 },   /* 500k - winner */
    { false,   0, 0 },   /* 1M */
};
#define RECOMMENDED_SLOT 2  /* 500 kbps */

/* ── ECU preset list (matching firmware/main/layout/ecu_presets.h shape) */
typedef struct { const char *make; const char *version; } preset_t;
static const preset_t PRESETS[] = {
    { "Haltech", "Elite 1500/2000/2500" },
    { "Haltech", "Nexus R5" },
    { "Link",    "G4+ / G4X" },
    { "MoTeC",   "M1 Series" },
    { "MaxxECU", "Mini / Street / Sport" },
    { "MaxxECU", "V1.2" },
    { "MS3-Pro", "PnP" },
    { "Ford",    "Falcon BA / BF" },
    { "Ford",    "Falcon FG" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define CUSTOM_LABEL "Custom / None"

/* ── WiFi scan mock data ─────────────────────────────────────────────── */
typedef struct { const char *ssid; int8_t rssi; bool secured; } mock_ap_t;
static const mock_ap_t MOCK_APS[] = {
    { "Home_2.4",     -42, true  },
    { "CarWiFi",      -55, true  },
    { "iPhone_Tommy", -58, true  },
    { "TeslaGuest",   -71, false },
    { "Neighbor5G",   -78, true  },
};
#define MOCK_AP_COUNT ((int)(sizeof(MOCK_APS) / sizeof(MOCK_APS[0])))

/* ── Global UI state ─────────────────────────────────────────────────── */
static lv_obj_t *s_overlay     = NULL;
static lv_obj_t *s_card        = NULL;

/* Step 1 (CAN scan) */
static lv_obj_t *s_step1           = NULL;
static lv_obj_t *s_scan_status     = NULL;
static lv_obj_t *s_scan_progress   = NULL;
static lv_obj_t *s_scan_bar        = NULL;
static lv_obj_t *s_scan_results[4] = {NULL};
static lv_obj_t *s_scan_detail     = NULL;
static lv_obj_t *s_btn_apply       = NULL;
static lv_obj_t *s_btn_next1       = NULL;
static lv_obj_t *s_btn_cancel      = NULL;

/* Scan state machine */
typedef enum { SCAN_IDLE = 0, SCAN_STOPPING, SCAN_TESTING, SCAN_RESTORING, SCAN_DONE } scan_state_t;
static scan_state_t s_scan_state = SCAN_IDLE;
static double       s_scan_started_ms = 0;
static lv_timer_t  *s_scan_timer = NULL;

/* Step 3 (Connect Your Device) */
static lv_obj_t *s_step3 = NULL;

/* ECU picker overlay (Step 2) */
static lv_obj_t *s_ecu_overlay = NULL;
static lv_obj_t *s_ecu_make_dd = NULL;
static lv_obj_t *s_ecu_ver_dd  = NULL;
static lv_obj_t *s_ecu_apply   = NULL;

/* WiFi picker + connect overlay */
static lv_obj_t *s_wifi_overlay = NULL;
static lv_obj_t *s_wifi_status  = NULL;
static lv_obj_t *s_wifi_list    = NULL;
static lv_timer_t *s_wifi_connect_timer = NULL;
static char      s_selected_ssid[33] = "";

/* Dashboard scene — mocks the firmware's default layout running with
 * plausible CAN driving data. See default_layout.c for the exact
 * widget positions + signal bindings we're reproducing. */
#define DASH_PANEL_COUNT 8
typedef struct {
    const char *label;
    const char *unit;
    int         decimals;
    float       baseline;  /* mock centre value */
    float       amplitude; /* mock oscillation */
    uint32_t    accent_rgb;/* 0 = no tinted border; otherwise 0xRRGGBB */
} dash_panel_def_t;

/* Labels match default_layout.c's panel_cfg[] (Haltech Nexus binding). */
static const dash_panel_def_t DASH_PANELS[DASH_PANEL_COUNT] = {
    { "IGNITION", "",    1, 18.0f,   6.0f,  0          },  /* 0: top-left   */
    { "MAP",      "kPa", 1, 95.0f,  40.0f,  0          },  /* 1: mid-left   */
    { "THROTTLE", "%",   0, 45.0f,  40.0f,  0          },  /* 2: bot-left   */
    { "COOLANT",  "C",   1, 82.0f,   4.0f,  0x2196F3   },  /* 3: bot-left   */
    { "INTAKE",   "C",   0, 34.0f,   6.0f,  0          },  /* 4: top-right  */
    { "LAMBDA",   "",    3,  0.92f,  0.08f, 0          },  /* 5: mid-right  */
    { "OIL TEMP", "C",   1, 95.0f,   5.0f,  0          },  /* 6: bot-right  */
    { "FUEL TRIM","%",   1,  1.5f,   2.0f,  0          },  /* 7: bot-right  */
};

/* Positions lifted verbatim from default_layout.c's s_panel_pos[]. */
static const struct { int16_t x, y; } DASH_PANEL_POS[DASH_PANEL_COUNT] = {
    {-312, -26}, {-146, -26}, {-312, 82}, {-146, 82},
    { 146, -26}, { 312, -26}, { 146, 82}, { 312, 82},
};

static lv_obj_t *s_dash_screen   = NULL;
static lv_obj_t *s_dash_rpm_bar  = NULL;
static lv_obj_t *s_dash_rpm_text = NULL;
static lv_obj_t *s_dash_speed    = NULL;
static lv_obj_t *s_dash_gear     = NULL;
static lv_obj_t *s_dash_coolant_bar = NULL;
static lv_obj_t *s_dash_throttle_bar = NULL;
static lv_obj_t *s_dash_panel_values[DASH_PANEL_COUNT] = {NULL};
static lv_obj_t *s_dash_ind_l    = NULL;
static lv_obj_t *s_dash_ind_r    = NULL;
static lv_timer_t *s_dash_timer  = NULL;
static double   s_dash_started_ms = 0;

/* ── Scene identifiers exported to JS ─────────────────────────────────
 * Tour scripts use these to rewind the LVGL state when the user taps
 * Back. Keep in sync with src/tutorial-runner.ts SCENE_MAP. */
#define SCENE_STEP1          0  /* CAN scan running (fresh start)    */
#define SCENE_STEP1_DONE     1  /* CAN scan complete, Apply visible  */
#define SCENE_STEP2          2  /* ECU picker overlay                */
#define SCENE_STEP3          3  /* Connect Your Device               */
#define SCENE_WIFI_PICKER    4  /* WiFi scan list overlay            */
#define SCENE_WIFI_CONNECTED 5  /* WiFi list with "Connected" status */
#define SCENE_DASHBOARD      6  /* Default layout "driving" with mock data */

/* ── Forward decls ───────────────────────────────────────────────────── */
static void _show_step1(void);
static void _show_step2(void);
static void _show_step3(void);
static void _scan_tick(lv_timer_t *t);
static void _scan_ui_update(void);
static void _close_wizard(void);
static void _rebuild_base_overlay(void);
static void _show_wifi_picker(void);
static void _show_dashboard(void);

/* ── Helpers ─────────────────────────────────────────────────────────── */

static lv_obj_t *_make_btn(lv_obj_t *parent, const char *text,
                           lv_color_t bg, lv_color_t fg,
                           bool border, lv_coord_t y,
                           lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    /* Transparent for "black" pseudo-buttons (skip/finish) so they blend
     * into the card background like the real wizard does. */
    lv_obj_set_style_bg_opa(btn,
        lv_color_brightness(bg) == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(btn, border ? 1 : 0, 0);
    lv_obj_set_style_border_color(btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, fg, 0);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ── Step 1: CAN scan ────────────────────────────────────────────────── */

static void _btn_cancel_scan_cb(lv_event_t *e) {
    (void)e;
    s_scan_state = SCAN_DONE;  /* force immediate completion */
    _scan_ui_update();
}

static void _btn_apply_cb(lv_event_t *e) {
    (void)e;
    _show_step2();
}

static void _btn_next1_cb(lv_event_t *e) {
    (void)e;
    _show_step2();
}

static void _btn_skip_cb(lv_event_t *e) {
    (void)e;
    _close_wizard();
}

static void _scan_tick(lv_timer_t *t) {
    (void)t;
    double elapsed = emscripten_get_now() - s_scan_started_ms;

    if (elapsed < 300) {
        s_scan_state = SCAN_STOPPING;
    } else if (elapsed < 3700) {
        s_scan_state = SCAN_TESTING;
    } else if (elapsed < 4100) {
        s_scan_state = SCAN_RESTORING;
    } else {
        s_scan_state = SCAN_DONE;
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    }
    _scan_ui_update();
}

static uint8_t _current_bitrate_idx(void) {
    double elapsed = emscripten_get_now() - s_scan_started_ms;
    if (elapsed < 300) return 0;
    /* Slots 0..3 across the 300..3700 ms window = ~850 ms each. */
    int idx = (int)((elapsed - 300) / 850);
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    return (uint8_t)idx;
}

static void _scan_ui_update(void) {
    if (!s_step1 || !lv_obj_is_valid(s_step1)) return;

    switch (s_scan_state) {
    case SCAN_STOPPING:
        lv_label_set_text(s_scan_status, "Stopping CAN for scan...");
        break;

    case SCAN_TESTING: {
        uint8_t idx = _current_bitrate_idx();
        lv_label_set_text(s_scan_status, "Scanning for CAN traffic...");
        lv_label_set_text_fmt(s_scan_progress,
            "Testing %s  (%d of 4)", BR_NAMES[idx], idx + 1);
        lv_bar_set_value(s_scan_bar, idx * 25, LV_ANIM_ON);

        for (uint8_t i = 0; i < 4; i++) {
            if (i < idx) {
                if (MOCK_RESULTS[i].traffic_detected) {
                    lv_label_set_text_fmt(s_scan_results[i],
                        "%s  --  %lu frames", BR_NAMES[i],
                        (unsigned long)MOCK_RESULTS[i].frames_received);
                    lv_obj_set_style_text_color(s_scan_results[i],
                        THEME_COLOR_STATUS_CONNECTED, 0);
                } else {
                    lv_label_set_text_fmt(s_scan_results[i],
                        "%s  --  No traffic", BR_NAMES[i]);
                    lv_obj_set_style_text_color(s_scan_results[i],
                        THEME_COLOR_TEXT_MUTED, 0);
                }
            } else if (i == idx) {
                lv_label_set_text_fmt(s_scan_results[i],
                    "%s  --  Testing...", BR_NAMES[i]);
                lv_obj_set_style_text_color(s_scan_results[i],
                    THEME_COLOR_ACCENT_YELLOW, 0);
            }
        }
        break;
    }

    case SCAN_RESTORING:
        lv_bar_set_value(s_scan_bar, 95, LV_ANIM_ON);
        lv_label_set_text(s_scan_status, "Restoring CAN...");
        break;

    case SCAN_DONE: {
        lv_bar_set_value(s_scan_bar, 100, LV_ANIM_ON);
        for (uint8_t i = 0; i < 4; i++) {
            if (MOCK_RESULTS[i].traffic_detected) {
                lv_label_set_text_fmt(s_scan_results[i],
                    "%s  --  %lu frames", BR_NAMES[i],
                    (unsigned long)MOCK_RESULTS[i].frames_received);
                lv_obj_set_style_text_color(s_scan_results[i],
                    THEME_COLOR_STATUS_CONNECTED, 0);
            } else {
                lv_label_set_text_fmt(s_scan_results[i],
                    "%s  --  No traffic", BR_NAMES[i]);
                lv_obj_set_style_text_color(s_scan_results[i],
                    THEME_COLOR_TEXT_MUTED, 0);
            }
        }

        lv_label_set_text_fmt(s_scan_status,
            "Detected CAN at %s", BR_NAMES[RECOMMENDED_SLOT]);
        lv_obj_set_style_text_color(s_scan_status,
            THEME_COLOR_STATUS_CONNECTED, 0);
        lv_label_set_text_fmt(s_scan_detail,
            "%lu frames, %u unique IDs",
            (unsigned long)MOCK_RESULTS[RECOMMENDED_SLOT].frames_received,
            MOCK_RESULTS[RECOMMENDED_SLOT].unique_id_count);

        lv_obj_t *lbl = lv_obj_get_child(s_btn_apply, 0);
        lv_label_set_text_fmt(lbl, "Apply %s & Continue", BR_NAMES[RECOMMENDED_SLOT]);
        lv_obj_clear_flag(s_btn_apply, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(s_btn_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_next1, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_scan_progress, "");
        break;
    }
    default: break;
    }
}

static void _show_step1(void) {
    s_step1 = lv_obj_create(s_card);
    lv_obj_remove_style_all(s_step1);
    lv_obj_set_size(s_step1, lv_pct(100), lv_pct(100));
    lv_obj_center(s_step1);
    lv_obj_clear_flag(s_step1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_step1);
    lv_label_set_text(title, "CAN Bus Setup");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *sub = lv_label_create(s_step1);
    lv_label_set_text(sub, "Step 1 of 3  -  Scanning all bitrates...");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_font(sub, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sub, THEME_COLOR_TEXT_MUTED, 0);

    s_scan_status = lv_label_create(s_step1);
    lv_label_set_text(s_scan_status, "Starting scan...");
    lv_obj_align(s_scan_status, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_text_font(s_scan_status, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_scan_status, THEME_COLOR_TEXT_PRIMARY, 0);

    s_scan_bar = lv_bar_create(s_step1);
    lv_obj_set_size(s_scan_bar, BTN_W, 8);
    lv_obj_align(s_scan_bar, LV_ALIGN_TOP_MID, 0, 86);
    lv_bar_set_range(s_scan_bar, 0, 100);
    lv_bar_set_value(s_scan_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_scan_bar, THEME_COLOR_SECTION_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_scan_bar, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_scan_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_scan_bar, 4, LV_PART_INDICATOR);

    s_scan_progress = lv_label_create(s_step1);
    lv_label_set_text(s_scan_progress, "");
    lv_obj_align(s_scan_progress, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_text_font(s_scan_progress, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_scan_progress, THEME_COLOR_TEXT_MUTED, 0);

    for (int i = 0; i < 4; i++) {
        s_scan_results[i] = lv_label_create(s_step1);
        lv_label_set_text_fmt(s_scan_results[i], "%s  --  ...", BR_NAMES[i]);
        lv_obj_align(s_scan_results[i], LV_ALIGN_TOP_LEFT, 20, 122 + i * 22);
        lv_obj_set_style_text_font(s_scan_results[i], THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_scan_results[i], THEME_COLOR_TEXT_MUTED, 0);
    }

    s_scan_detail = lv_label_create(s_step1);
    lv_label_set_text(s_scan_detail, "");
    lv_label_set_long_mode(s_scan_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_scan_detail, lv_pct(90));
    lv_obj_align(s_scan_detail, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_set_style_text_font(s_scan_detail, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_scan_detail, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(s_scan_detail, LV_TEXT_ALIGN_CENTER, 0);

    s_btn_apply = _make_btn(s_step1, "Apply & Continue",
                            THEME_COLOR_ACCENT_BLUE, THEME_COLOR_TEXT_ON_ACCENT,
                            false, 248, _btn_apply_cb);
    lv_obj_add_flag(s_btn_apply, LV_OBJ_FLAG_HIDDEN);

    s_btn_cancel = _make_btn(s_step1, "Cancel Scan",
                             THEME_COLOR_SECTION_BG, THEME_COLOR_TEXT_MUTED,
                             true, 296, _btn_cancel_scan_cb);

    s_btn_next1 = _make_btn(s_step1, "Continue without CAN",
                            THEME_COLOR_SECTION_BG, THEME_COLOR_TEXT_MUTED,
                            true, 296, _btn_next1_cb);
    lv_obj_add_flag(s_btn_next1, LV_OBJ_FLAG_HIDDEN);

    _make_btn(s_step1, "Skip for now  (ask again next boot)",
              lv_color_black(), THEME_COLOR_TEXT_MUTED,
              false, 340, _btn_skip_cb);

    /* Kick off the scan immediately on mount. */
    s_scan_state = SCAN_STOPPING;
    s_scan_started_ms = emscripten_get_now();
    if (s_scan_timer) lv_timer_del(s_scan_timer);
    s_scan_timer = lv_timer_create(_scan_tick, 100, NULL);
    _scan_ui_update();
}

/* ── Step 2: ECU picker overlay ──────────────────────────────────────── */

static void _ecu_update_versions(void) {
    if (!s_ecu_make_dd || !s_ecu_ver_dd) return;

    char make[48] = {0};
    lv_dropdown_get_selected_str(s_ecu_make_dd, make, sizeof(make));
    bool is_custom = (strcmp(make, CUSTOM_LABEL) == 0);

    if (is_custom) {
        lv_dropdown_set_options(s_ecu_ver_dd, "(none)");
        lv_obj_add_state(s_ecu_ver_dd, LV_STATE_DISABLED);
    } else {
        char buf[256] = {0};
        bool first = true;
        for (int i = 0; i < PRESET_COUNT; i++) {
            if (strcmp(PRESETS[i].make, make) == 0) {
                if (!first) strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, PRESETS[i].version, sizeof(buf) - strlen(buf) - 1);
                first = false;
            }
        }
        lv_dropdown_set_options(s_ecu_ver_dd, buf[0] ? buf : "(none)");
        lv_dropdown_set_selected(s_ecu_ver_dd, 0);
        lv_obj_clear_state(s_ecu_ver_dd, LV_STATE_DISABLED);
    }
}

static void _ecu_make_changed_cb(lv_event_t *e) {
    (void)e;
    _ecu_update_versions();
}

static void _ecu_close(void) {
    if (s_ecu_overlay && lv_obj_is_valid(s_ecu_overlay)) {
        lv_obj_del(s_ecu_overlay);
    }
    s_ecu_overlay = NULL;
    s_ecu_make_dd = s_ecu_ver_dd = s_ecu_apply = NULL;
}

static void _ecu_apply_cb(lv_event_t *e) {
    (void)e;
    _ecu_close();
    _show_step3();
}

static void _ecu_skip_cb(lv_event_t *e) {
    (void)e;
    _ecu_close();
    _show_step3();
}

static void _show_step2(void) {
    /* Remove step 1 content. */
    if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    if (s_step1 && lv_obj_is_valid(s_step1)) lv_obj_del(s_step1);
    s_step1 = NULL;

    /* Overlay on top layer (matches firmware's ecu_picker_open). */
    s_ecu_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ecu_overlay);
    lv_obj_set_size(s_ecu_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(s_ecu_overlay);
    lv_obj_set_style_bg_color(s_ecu_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ecu_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(s_ecu_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(s_ecu_overlay);
    lv_obj_set_size(card, 600, 360);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, THEME_COLOR_PANEL, 0);
    lv_obj_set_style_border_color(card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Select Your ECU");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Step 2 of 3  -  Picks the CAN decoder for your engine");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_font(sub, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sub, THEME_COLOR_TEXT_MUTED, 0);

    /* Make label + dropdown */
    lv_obj_t *make_lbl = lv_label_create(card);
    lv_label_set_text(make_lbl, "Make");
    lv_obj_align(make_lbl, LV_ALIGN_TOP_LEFT, 20, 74);
    lv_obj_set_style_text_font(make_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(make_lbl, THEME_COLOR_TEXT_MUTED, 0);

    /* Build unique makes list + Custom pseudo-option. */
    char makes[256] = {0};
    for (int i = 0; i < PRESET_COUNT; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(PRESETS[j].make, PRESETS[i].make) == 0) { dup = true; break; }
        }
        if (dup) continue;
        if (makes[0]) strncat(makes, "\n", sizeof(makes) - strlen(makes) - 1);
        strncat(makes, PRESETS[i].make, sizeof(makes) - strlen(makes) - 1);
    }
    strncat(makes, "\n", sizeof(makes) - strlen(makes) - 1);
    strncat(makes, CUSTOM_LABEL, sizeof(makes) - strlen(makes) - 1);

    s_ecu_make_dd = lv_dropdown_create(card);
    lv_dropdown_set_options(s_ecu_make_dd, makes);
    lv_obj_align(s_ecu_make_dd, LV_ALIGN_TOP_LEFT, 20, 96);
    lv_obj_set_size(s_ecu_make_dd, 260, 40);
    lv_obj_set_style_bg_color(s_ecu_make_dd, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_text_color(s_ecu_make_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_ecu_make_dd, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(s_ecu_make_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ecu_make_dd, 1, 0);
    lv_obj_set_style_radius(s_ecu_make_dd, THEME_RADIUS_NORMAL, 0);
    lv_obj_add_event_cb(s_ecu_make_dd, _ecu_make_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Version label + dropdown */
    lv_obj_t *ver_lbl = lv_label_create(card);
    lv_label_set_text(ver_lbl, "Version");
    lv_obj_align(ver_lbl, LV_ALIGN_TOP_LEFT, 300, 74);
    lv_obj_set_style_text_font(ver_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ver_lbl, THEME_COLOR_TEXT_MUTED, 0);

    s_ecu_ver_dd = lv_dropdown_create(card);
    lv_obj_align(s_ecu_ver_dd, LV_ALIGN_TOP_LEFT, 300, 96);
    lv_obj_set_size(s_ecu_ver_dd, 260, 40);
    lv_obj_set_style_bg_color(s_ecu_ver_dd, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_text_color(s_ecu_ver_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_ecu_ver_dd, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(s_ecu_ver_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ecu_ver_dd, 1, 0);
    lv_obj_set_style_radius(s_ecu_ver_dd, THEME_RADIUS_NORMAL, 0);

    _ecu_update_versions();

    /* Hint */
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint,
        "Applying will configure the default layout's signal bindings.\n"
        "You can change this later in Device Settings.");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_text_font(hint, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(hint, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    /* Apply + Skip */
    s_ecu_apply = _make_btn(card, "Apply",
                            THEME_COLOR_ACCENT_BLUE, THEME_COLOR_TEXT_ON_ACCENT,
                            false, 220, _ecu_apply_cb);

    _make_btn(card, "Skip  (configure signals manually later)",
              lv_color_black(), THEME_COLOR_TEXT_MUTED,
              false, 272, _ecu_skip_cb);
}

/* ── WiFi scan + connect overlay ─────────────────────────────────────── */

static int8_t _rssi_bars(int8_t rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    return 1;
}

static void _wifi_close(void) {
    if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
    if (s_wifi_overlay && lv_obj_is_valid(s_wifi_overlay)) {
        lv_obj_del(s_wifi_overlay);
    }
    s_wifi_overlay = NULL;
    s_wifi_status = s_wifi_list = NULL;
}

static void _wifi_connect_complete(lv_timer_t *t) {
    lv_timer_del(t);
    s_wifi_connect_timer = NULL;
    if (s_wifi_status && lv_obj_is_valid(s_wifi_status)) {
        lv_label_set_text_fmt(s_wifi_status, "Connected to %s", s_selected_ssid);
        lv_obj_set_style_text_color(s_wifi_status, THEME_COLOR_STATUS_CONNECTED, 0);
    }
}

static void _wifi_ap_clicked_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = (const char *)lv_obj_get_user_data(btn);
    if (!ssid) return;
    snprintf(s_selected_ssid, sizeof(s_selected_ssid), "%s", ssid);
    lv_label_set_text_fmt(s_wifi_status, "Connecting to %s...", ssid);
    lv_obj_set_style_text_color(s_wifi_status, THEME_COLOR_ACCENT_YELLOW, 0);
    if (s_wifi_connect_timer) lv_timer_del(s_wifi_connect_timer);
    s_wifi_connect_timer = lv_timer_create(_wifi_connect_complete, 1800, NULL);
    lv_timer_set_repeat_count(s_wifi_connect_timer, 1);
}

static void _wifi_back_cb(lv_event_t *e) {
    (void)e;
    _wifi_close();
}

static void _show_wifi_picker(void) {
    s_wifi_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wifi_overlay);
    lv_obj_set_size(s_wifi_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(s_wifi_overlay);
    lv_obj_set_style_bg_color(s_wifi_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_wifi_overlay, LV_OBJ_FLAG_CLICKABLE);

    /* Title bar */
    lv_obj_t *title = lv_label_create(s_wifi_overlay);
    lv_label_set_text(title, "WiFi");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 30, 20);
    lv_obj_set_style_text_font(title, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    /* Back button */
    lv_obj_t *back = lv_btn_create(s_wifi_overlay);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -30, 20);
    lv_obj_set_style_bg_color(back, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_border_color(back, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, THEME_RADIUS_NORMAL, 0);
    lv_obj_add_event_cb(back, _wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_center(bl);
    lv_obj_set_style_text_font(bl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bl, THEME_COLOR_TEXT_PRIMARY, 0);

    /* Status line */
    s_wifi_status = lv_label_create(s_wifi_overlay);
    lv_label_set_text(s_wifi_status, "Available networks:");
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 30, 68);
    lv_obj_set_style_text_font(s_wifi_status, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_wifi_status, THEME_COLOR_TEXT_MUTED, 0);

    /* Scan list */
    s_wifi_list = lv_obj_create(s_wifi_overlay);
    lv_obj_set_size(s_wifi_list, 740, 340);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_bg_color(s_wifi_list, THEME_COLOR_PANEL, 0);
    lv_obj_set_style_border_color(s_wifi_list, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_wifi_list, 1, 0);
    lv_obj_set_style_radius(s_wifi_list, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 10, 0);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (int i = 0; i < MOCK_AP_COUNT; i++) {
        lv_obj_t *row = lv_btn_create(s_wifi_list);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_bg_color(row, THEME_COLOR_SECTION_BG, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, THEME_RADIUS_NORMAL, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_user_data(row, (void *)MOCK_APS[i].ssid);
        lv_obj_add_event_cb(row, _wifi_ap_clicked_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, MOCK_APS[i].ssid);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 0, -6);
        lv_obj_set_style_text_font(ssid_lbl, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(ssid_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

        lv_obj_t *meta = lv_label_create(row);
        lv_label_set_text_fmt(meta, "%s  *  %d dBm  *  %d/4 bars",
                              MOCK_APS[i].secured ? "WPA2" : "Open",
                              MOCK_APS[i].rssi,
                              _rssi_bars(MOCK_APS[i].rssi));
        lv_obj_align(meta, LV_ALIGN_LEFT_MID, 0, 10);
        lv_obj_set_style_text_font(meta, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(meta, THEME_COLOR_TEXT_MUTED, 0);
    }
}

/* ── Step 3: Connect Your Device ─────────────────────────────────────── */

static void _btn_wifi_join_cb(lv_event_t *e) {
    (void)e;
    _show_wifi_picker();
}

static void _btn_finish_cb(lv_event_t *e) {
    (void)e;
    _close_wizard();
}

static void _show_step3(void) {
    /* Fresh container on the card. */
    if (s_step3 && lv_obj_is_valid(s_step3)) lv_obj_del(s_step3);

    s_step3 = lv_obj_create(s_card);
    lv_obj_remove_style_all(s_step3);
    lv_obj_set_size(s_step3, lv_pct(100), lv_pct(100));
    lv_obj_center(s_step3);
    lv_obj_clear_flag(s_step3, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_step3);
    lv_label_set_text(title, "Connect Your Device");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *sub = lv_label_create(s_step3);
    lv_label_set_text(sub, "Step 3 of 3  -  you have a few ways to connect");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_text_font(sub, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sub, THEME_COLOR_TEXT_MUTED, 0);

    /* ── Option 1: WiFi (recommended) ── */
    lv_obj_t *o1 = lv_label_create(s_step3);
    lv_label_set_text(o1, "1.  WiFi  (recommended)");
    lv_obj_align(o1, LV_ALIGN_TOP_LEFT, 30, 66);
    lv_obj_set_style_text_font(o1, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(o1, THEME_COLOR_ACCENT_BLUE, 0);

    lv_obj_t *o1s = lv_label_create(s_step3);
    lv_label_set_text(o1s,
        "Join your home/shop WiFi - dash will show its IP in Device Settings");
    lv_obj_align(o1s, LV_ALIGN_TOP_LEFT, 30, 86);
    lv_obj_set_style_text_font(o1s, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(o1s, THEME_COLOR_TEXT_MUTED, 0);

    _make_btn(s_step3, "Join a WiFi Network",
              THEME_COLOR_ACCENT_BLUE, THEME_COLOR_TEXT_ON_ACCENT,
              false, 108, _btn_wifi_join_cb);

    /* ── Option 2: USB ── */
    lv_obj_t *o2 = lv_label_create(s_step3);
    lv_label_set_text(o2, "2.  USB");
    lv_obj_align(o2, LV_ALIGN_TOP_LEFT, 30, 164);
    lv_obj_set_style_text_font(o2, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(o2, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *o2s = lv_label_create(s_step3);
    lv_label_set_long_mode(o2s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(o2s, BTN_W - 30);
    lv_label_set_text(o2s,
        "Plug the UART USB port into your laptop - the RDM Desktop Studio app "
        "will detect the device automatically. No setup required.");
    lv_obj_align(o2s, LV_ALIGN_TOP_LEFT, 30, 184);
    lv_obj_set_style_text_font(o2s, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(o2s, THEME_COLOR_TEXT_MUTED, 0);

    /* ── Option 3: Hotspot ── */
    lv_obj_t *o3 = lv_label_create(s_step3);
    lv_label_set_text(o3, "3.  Hotspot  (fallback, no WiFi needed)");
    lv_obj_align(o3, LV_ALIGN_TOP_LEFT, 30, 230);
    lv_obj_set_style_text_font(o3, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(o3, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *o3s = lv_label_create(s_step3);
    lv_label_set_long_mode(o3s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(o3s, BTN_W - 30);
    /* RDM7-XXXX is the obvious-placeholder form of what the firmware
     * shows — on a real device the "XXXX" is the last four hex chars
     * of the MAC (e.g. RDM7-7B0D). Users see their actual SSID on
     * the splash screen. */
    lv_label_set_text(o3s,
        "Connect to \"RDM7-XXXX\"  /  password: rdm7dash\n"
        "Then open http://192.168.4.1 in a browser");
    lv_obj_align(o3s, LV_ALIGN_TOP_LEFT, 30, 250);
    lv_obj_set_style_text_font(o3s, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(o3s, THEME_COLOR_TEXT_MUTED, 0);

    _make_btn(s_step3, "Finish Setup",
              lv_color_black(), THEME_COLOR_TEXT_MUTED,
              false, 296, _btn_finish_cb);
}

/* ── Dashboard scene ──────────────────────────────────────────────────
 *
 * Shown at the end of the Guided Tour. Mirrors the firmware's
 * default layout (default_layout.c) running with plausible CAN
 * driving data — the "payoff" frame that proves everything we just
 * set up actually works. No real CAN; values are advanced by a
 * local 60 ms timer that models a short acceleration loop.           */

static float _wave(double elapsed_ms, float baseline, float amplitude,
                   double period_ms, double phase_ms) {
    double t  = (elapsed_ms - phase_ms) / period_ms;
    double v  = baseline + amplitude * sin(t * 2.0 * 3.14159265);
    return (float)v;
}

/* "Drive cycle" — simple state machine that looks natural:
 *   0  : idle 2 s
 *   2  : accelerating (throttle ramps up, RPM climbs, speed rises)
 *   7  : shift (quick RPM drop), then more acceleration
 *   12 : cruising (throttle ~30 %, RPM ~3800)
 *   16 : loop
 */
static void _drive_state(double ms, float *rpm, float *speed, float *throttle, int *gear) {
    double t = ms / 1000.0;
    double cycle = fmod(t, 16.0);

    if (cycle < 2.0) {
        *rpm = 950.0f + (float)cycle * 40.0f;
        *speed = 0.0f;
        *throttle = 5.0f;
        *gear = 0;
    } else if (cycle < 5.5) {
        float p = (float)((cycle - 2.0) / 3.5);
        *rpm = 1800.0f + p * 4500.0f;
        *speed = p * 85.0f;
        *throttle = 70.0f + (float)sin(t * 4.0) * 20.0f;
        *gear = 1 + (int)(p * 2.0f);
    } else if (cycle < 7.0) {
        /* Shift: throttle dips, RPM drops, speed continues. */
        *rpm = 3800.0f;
        *speed = 88.0f;
        *throttle = 10.0f;
        *gear = 3;
    } else if (cycle < 10.5) {
        float p = (float)((cycle - 7.0) / 3.5);
        *rpm = 3200.0f + p * 3200.0f;
        *speed = 90.0f + p * 55.0f;
        *throttle = 80.0f;
        *gear = 4;
    } else {
        /* Cruise. */
        *rpm = 3600.0f + (float)sin(t * 3.0) * 200.0f;
        *speed = 142.0f + (float)sin(t * 2.0) * 3.0f;
        *throttle = 28.0f + (float)sin(t * 5.0) * 4.0f;
        *gear = 5;
    }

    if (*rpm > 6700.0f) *rpm = 6700.0f;
    if (*throttle < 0)   *throttle = 0;
    if (*throttle > 100) *throttle = 100;
}

static void _dash_tick(lv_timer_t *t) {
    (void)t;
    if (!s_dash_screen || !lv_obj_is_valid(s_dash_screen)) return;
    double ms = emscripten_get_now() - s_dash_started_ms;

    float rpm, speed, throttle;
    int gear;
    _drive_state(ms, &rpm, &speed, &throttle, &gear);

    /* RPM bar + big RPM label above gear */
    if (s_dash_rpm_bar)  lv_bar_set_value(s_dash_rpm_bar,  (int)rpm, LV_ANIM_OFF);
    if (s_dash_rpm_text) lv_label_set_text_fmt(s_dash_rpm_text, "%d", (int)rpm);
    /* Speed */
    if (s_dash_speed)    lv_label_set_text_fmt(s_dash_speed, "%d", (int)speed);
    /* Gear */
    if (s_dash_gear)     lv_label_set_text_fmt(s_dash_gear, "%d", gear < 0 ? 0 : gear);
    /* Throttle bar */
    if (s_dash_throttle_bar) lv_bar_set_value(s_dash_throttle_bar, (int)throttle, LV_ANIM_OFF);

    /* Panels: oscillate around their baselines so the dash feels alive. */
    for (int i = 0; i < DASH_PANEL_COUNT; i++) {
        if (!s_dash_panel_values[i]) continue;
        float v;
        if (i == 2) {
            /* THROTTLE panel mirrors the throttle bar. */
            v = throttle;
        } else if (i == 3) {
            /* COOLANT slowly rises then holds around 85. */
            v = 82.0f + (float)fmin(ms / 40000.0, 5.0);
        } else {
            v = _wave(ms, DASH_PANELS[i].baseline, DASH_PANELS[i].amplitude,
                     3000.0 + i * 350.0, i * 700.0);
        }
        char buf[32];
        if (DASH_PANELS[i].decimals == 0) snprintf(buf, sizeof(buf), "%d", (int)v);
        else if (DASH_PANELS[i].decimals == 1) snprintf(buf, sizeof(buf), "%.1f", v);
        else snprintf(buf, sizeof(buf), "%.3f", v);
        lv_label_set_text(s_dash_panel_values[i], buf);
    }

    /* Coolant bar tracks the COOLANT panel value (range 0-120). */
    if (s_dash_coolant_bar) {
        float coolant = 82.0f + (float)fmin(ms / 40000.0, 5.0);
        lv_bar_set_value(s_dash_coolant_bar, (int)coolant, LV_ANIM_OFF);
    }

    /* Blink the left indicator every ~600 ms after 6 s to look like
     * a real driver intending a turn. */
    if (s_dash_ind_l) {
        bool on = ms > 6000 && ms < 10000 && ((int)(ms / 500) & 1);
        lv_obj_set_style_bg_opa(s_dash_ind_l, on ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

/* One small panel from default_layout.c's widget_panel mock. */
static lv_obj_t *_make_dash_panel(lv_obj_t *parent, const dash_panel_def_t *d,
                                  int16_t x, int16_t y) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, 155, 92);
    lv_obj_align(p, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    /* Thin border — default_layout uses the panel widget's built-in
     * border; here a subtle dark grey reads cleanly on pure black. */
    lv_color_t border = d->accent_rgb
        ? lv_color_hex(d->accent_rgb)
        : lv_color_hex(0x2a2a2a);
    lv_obj_set_style_border_color(p, border, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 4, 0);
    lv_obj_set_style_pad_all(p, 6, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(p);
    lv_label_set_text(lbl, d->label);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xb0b0b0), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    lv_obj_t *val = lv_label_create(p);
    lv_label_set_text(val, "--");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    return val;
}

static void _show_dashboard(void) {
    /* Take over the whole screen — no overlay card, no bezel decoration.
     * This mirrors what the dash does on boot once first_run_done is set. */
    _rebuild_base_overlay();
    if (s_overlay && lv_obj_is_valid(s_overlay)) lv_obj_del(s_overlay);
    s_overlay = s_card = NULL;

    s_dash_screen = lv_scr_act();
    lv_obj_clean(s_dash_screen);
    lv_obj_set_style_bg_color(s_dash_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dash_screen, LV_OPA_COVER, 0);

    /* ── RPM bar across top (matches default_layout x=0, y=-215, 800x55) */
    s_dash_rpm_bar = lv_bar_create(s_dash_screen);
    lv_obj_set_size(s_dash_rpm_bar, 800, 40);
    lv_obj_align(s_dash_rpm_bar, LV_ALIGN_CENTER, 0, -215);
    lv_bar_set_range(s_dash_rpm_bar, 0, 7000);
    lv_bar_set_value(s_dash_rpm_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_dash_rpm_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dash_rpm_bar, lv_color_hex(0x4ade80), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dash_rpm_bar, 0, 0);
    lv_obj_set_style_radius(s_dash_rpm_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_dash_rpm_bar, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(s_dash_rpm_bar, 1, 0);

    /* Redline zone — a slim red block at the right end. Static, drawn
     * once; the actual bar fill animates in _dash_tick. */
    lv_obj_t *redline = lv_obj_create(s_dash_screen);
    lv_obj_set_size(redline, 72, 40);  /* 72/800 ≈ 9 % ≈ 6500-7000 band */
    lv_obj_align(redline, LV_ALIGN_CENTER, 364, -215);
    lv_obj_set_style_bg_color(redline, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_bg_opa(redline, LV_OPA_30, 0);
    lv_obj_set_style_border_width(redline, 0, 0);
    lv_obj_set_style_radius(redline, 0, 0);

    /* ── 8 warning-light circles at top (y=-148) ─────────────────────── */
    static const int16_t WARN_X[8] = { -352, -292, -232, -172, 172, 232, 292, 352 };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *w = lv_obj_create(s_dash_screen);
        lv_obj_set_size(w, 20, 20);
        lv_obj_align(w, LV_ALIGN_CENTER, WARN_X[i], -148);
        lv_obj_set_style_radius(w, 10, 0);
        /* Warnings 0 + 3 "active" for flavour (ABS OFF, HEAD LIGHTS). */
        bool active = (i == 0 || i == 3);
        lv_obj_set_style_bg_color(w, active ? lv_color_hex(0xef4444) : lv_color_hex(0x3a3a3a), 0);
        lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(w, 0, 0);
    }

    /* ── Left / right indicators (arrows around RPM number) ─────────── */
    s_dash_ind_l = lv_label_create(s_dash_screen);
    lv_label_set_text(s_dash_ind_l, LV_SYMBOL_LEFT);
    lv_obj_align(s_dash_ind_l, LV_ALIGN_CENTER, -95, -133);
    lv_obj_set_style_text_color(s_dash_ind_l, lv_color_hex(0x4ade80), 0);
    lv_obj_set_style_text_font(s_dash_ind_l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_opa(s_dash_ind_l, LV_OPA_20, 0);

    s_dash_ind_r = lv_label_create(s_dash_screen);
    lv_label_set_text(s_dash_ind_r, LV_SYMBOL_RIGHT);
    lv_obj_align(s_dash_ind_r, LV_ALIGN_CENTER, 95, -133);
    lv_obj_set_style_text_color(s_dash_ind_r, lv_color_hex(0x4ade80), 0);
    lv_obj_set_style_text_font(s_dash_ind_r, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_opa(s_dash_ind_r, LV_OPA_20, 0);

    /* ── RPM big number centre-top ──────────────────────────────────── */
    lv_obj_t *rpm_lbl = lv_label_create(s_dash_screen);
    lv_label_set_text(rpm_lbl, "RPM");
    lv_obj_align(rpm_lbl, LV_ALIGN_CENTER, 0, -165);
    lv_obj_set_style_text_color(rpm_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(rpm_lbl, &lv_font_montserrat_10, 0);

    s_dash_rpm_text = lv_label_create(s_dash_screen);
    lv_label_set_text(s_dash_rpm_text, "0");
    lv_obj_align(s_dash_rpm_text, LV_ALIGN_CENTER, 0, -133);
    lv_obj_set_style_text_color(s_dash_rpm_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_dash_rpm_text, &lv_font_montserrat_24, 0);

    /* ── RDM logo (placeholder text) ────────────────────────────────── */
    lv_obj_t *logo = lv_label_create(s_dash_screen);
    lv_label_set_text(logo, "R D M");
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_text_color(logo, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_22, 0);

    /* ── Speed centre (the "136" in the reference) ──────────────────── */
    s_dash_speed = lv_label_create(s_dash_screen);
    lv_label_set_text(s_dash_speed, "0");
    lv_obj_align(s_dash_speed, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_color(s_dash_speed, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_dash_speed, &lv_font_montserrat_48, 0);

    lv_obj_t *kph = lv_label_create(s_dash_screen);
    lv_label_set_text(kph, "km/h");
    lv_obj_align(kph, LV_ALIGN_CENTER, 0, 72);
    lv_obj_set_style_text_color(kph, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(kph, &lv_font_montserrat_10, 0);

    /* ── 8 panels ───────────────────────────────────────────────────── */
    for (int i = 0; i < DASH_PANEL_COUNT; i++) {
        s_dash_panel_values[i] = _make_dash_panel(s_dash_screen, &DASH_PANELS[i],
                                                   DASH_PANEL_POS[i].x,
                                                   DASH_PANEL_POS[i].y);
    }

    /* ── Gear box centre-bottom (x=0, y=178, 92x92) ─────────────────── */
    lv_obj_t *gearp = lv_obj_create(s_dash_screen);
    lv_obj_set_size(gearp, 92, 92);
    lv_obj_align(gearp, LV_ALIGN_CENTER, 0, 178);
    lv_obj_set_style_bg_color(gearp, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(gearp, lv_color_hex(0x3a3a3a), 0);
    lv_obj_set_style_border_width(gearp, 1, 0);
    lv_obj_set_style_radius(gearp, 4, 0);
    lv_obj_set_style_pad_all(gearp, 4, 0);
    lv_obj_clear_flag(gearp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *gl = lv_label_create(gearp);
    lv_label_set_text(gl, "GEAR");
    lv_obj_align(gl, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_color(gl, lv_color_hex(0xb0b0b0), 0);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_12, 0);

    s_dash_gear = lv_label_create(gearp);
    lv_label_set_text(s_dash_gear, "0");
    lv_obj_align(s_dash_gear, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_color(s_dash_gear, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_dash_gear, &lv_font_montserrat_48, 0);

    /* ── Coolant + Throttle bars at y=209 ───────────────────────────── */
    s_dash_coolant_bar = lv_bar_create(s_dash_screen);
    lv_obj_set_size(s_dash_coolant_bar, 300, 22);
    lv_obj_align(s_dash_coolant_bar, LV_ALIGN_CENTER, -240, 213);
    lv_bar_set_range(s_dash_coolant_bar, 0, 120);
    lv_bar_set_value(s_dash_coolant_bar, 60, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_dash_coolant_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dash_coolant_bar, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dash_coolant_bar, 2, 0);
    lv_obj_set_style_radius(s_dash_coolant_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_dash_coolant_bar, 0, 0);

    lv_obj_t *cbl = lv_label_create(s_dash_screen);
    lv_label_set_text(cbl, "COOLANT TEMP");
    lv_obj_align(cbl, LV_ALIGN_CENTER, -240, 186);
    lv_obj_set_style_text_color(cbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(cbl, &lv_font_montserrat_10, 0);

    s_dash_throttle_bar = lv_bar_create(s_dash_screen);
    lv_obj_set_size(s_dash_throttle_bar, 300, 22);
    lv_obj_align(s_dash_throttle_bar, LV_ALIGN_CENTER, 240, 213);
    lv_bar_set_range(s_dash_throttle_bar, 0, 100);
    lv_bar_set_value(s_dash_throttle_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_dash_throttle_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dash_throttle_bar, lv_color_hex(0x4ade80), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dash_throttle_bar, 2, 0);
    lv_obj_set_style_radius(s_dash_throttle_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_dash_throttle_bar, 0, 0);

    lv_obj_t *tbl = lv_label_create(s_dash_screen);
    lv_label_set_text(tbl, "THROTTLE %");
    lv_obj_align(tbl, LV_ALIGN_CENTER, 240, 186);
    lv_obj_set_style_text_color(tbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(tbl, &lv_font_montserrat_10, 0);

    /* Start the drive animation. */
    s_dash_started_ms = emscripten_get_now();
    if (s_dash_timer) lv_timer_del(s_dash_timer);
    s_dash_timer = lv_timer_create(_dash_tick, 60, NULL);
}

/* ── Teardown / public entry ─────────────────────────────────────────── */

static void _close_wizard(void) {
    if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
    _wifi_close();
    _ecu_close();
    if (s_overlay && lv_obj_is_valid(s_overlay)) lv_obj_del(s_overlay);
    s_overlay = s_card = s_step1 = s_step3 = NULL;
    /* Show a post-wizard "ready" blurb. */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, "Setup Complete");
    lv_obj_center(t);
    lv_obj_set_style_text_font(t, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(t, THEME_COLOR_ACCENT_BLUE, 0);
}

/* Rebuild the overlay + card from scratch. Called on initial start and
 * on every scene jump (Back button in the tour) so each scene starts
 * from a guaranteed-clean state. Cheap: LVGL object creation is fast. */
static void _rebuild_base_overlay(void) {
    /* Tear down any lingering state first. */
    if (s_scan_timer)         { lv_timer_del(s_scan_timer);         s_scan_timer         = NULL; }
    if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
    if (s_dash_timer)         { lv_timer_del(s_dash_timer);         s_dash_timer         = NULL; }
    s_dash_screen = s_dash_rpm_bar = s_dash_rpm_text = s_dash_speed = NULL;
    s_dash_gear = s_dash_coolant_bar = s_dash_throttle_bar = NULL;
    s_dash_ind_l = s_dash_ind_r = NULL;
    for (int i = 0; i < DASH_PANEL_COUNT; i++) s_dash_panel_values[i] = NULL;
    if (s_ecu_overlay && lv_obj_is_valid(s_ecu_overlay))   lv_obj_del(s_ecu_overlay);
    if (s_wifi_overlay && lv_obj_is_valid(s_wifi_overlay)) lv_obj_del(s_wifi_overlay);
    s_ecu_overlay = s_ecu_make_dd = s_ecu_ver_dd = s_ecu_apply = NULL;
    s_wifi_overlay = s_wifi_status = s_wifi_list = NULL;

    /* Replace the screen so stray orphan objects don't accumulate across
     * scene changes — LVGL cleans up everything parented to the old
     * screen when it's deleted. */
    lv_obj_t *old_scr = lv_scr_act();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(scr);
    if (old_scr && lv_obj_is_valid(old_scr)) lv_obj_del(old_scr);

    s_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);

    s_card = lv_obj_create(s_overlay);
    lv_obj_set_size(s_card, CARD_W, CARD_H);
    lv_obj_center(s_card);
    lv_obj_set_style_bg_color(s_card, THEME_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_radius(s_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(s_card, 20, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    s_step1 = s_step3 = NULL;
    s_scan_status = s_scan_progress = s_scan_bar = s_scan_detail = NULL;
    s_btn_apply = s_btn_next1 = s_btn_cancel = NULL;
    for (int i = 0; i < 4; i++) s_scan_results[i] = NULL;
}

/* Jump the wizard to an arbitrary scene. Exported to JS so the tour
 * runner can rewind the LVGL render to match audio rewind (Back
 * button). Scenes are integer codes — see the #defines above. */
EMSCRIPTEN_KEEPALIVE
void sandbox_set_scene(int scene) {
    _rebuild_base_overlay();

    switch (scene) {
    case SCENE_STEP1:
        _show_step1();
        break;

    case SCENE_STEP1_DONE:
        _show_step1();
        /* Fast-forward the scan state machine to "done" so the UI shows
         * the final result without waiting the 4 s animation. */
        s_scan_state = SCAN_DONE;
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
        _scan_ui_update();
        break;

    case SCENE_STEP2:
        /* Build step1 as the card backdrop (it sits behind the ECU overlay
         * in the real firmware too) then layer the picker on top. */
        _show_step1();
        s_scan_state = SCAN_DONE;
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
        _scan_ui_update();
        _show_step2();
        break;

    case SCENE_STEP3:
        _show_step3();
        break;

    case SCENE_WIFI_PICKER:
        _show_step3();
        _show_wifi_picker();
        break;

    case SCENE_WIFI_CONNECTED: {
        _show_step3();
        _show_wifi_picker();
        /* Skip the 1.8 s fake-connect timer and jump straight to the
         * connected state so the caption line matches the visual. */
        if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
        snprintf(s_selected_ssid, sizeof(s_selected_ssid), "%s", "Home_2.4");
        if (s_wifi_status && lv_obj_is_valid(s_wifi_status)) {
            lv_label_set_text_fmt(s_wifi_status, "Connected to %s", s_selected_ssid);
            lv_obj_set_style_text_color(s_wifi_status, THEME_COLOR_STATUS_CONNECTED, 0);
        }
        break;
    }

    case SCENE_DASHBOARD:
        _show_dashboard();
        break;

    default:
        /* Unknown scene -- fall back to the start. */
        _show_step1();
        break;
    }
}

void wizard_sandbox_start(void) {
    sandbox_set_scene(SCENE_STEP1);
}
