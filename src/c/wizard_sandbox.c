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
#include "cJSON.h"
#include "esp_idf_shim.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ── Theme (copied verbatim from firmware/main/ui/theme.h) ───────────── */
#define THEME_COLOR_BG               lv_color_hex(0x000000)
#define THEME_COLOR_SURFACE          lv_color_hex(0x292C29)
#define THEME_COLOR_PANEL            lv_color_hex(0x393C39)
#define THEME_COLOR_SECTION_BG       lv_color_hex(0x393C39)
#define THEME_COLOR_INPUT_BG         lv_color_hex(0x181C18)
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

/* ── ECU preset list — MUST MATCH main/layout/ecu_presets.c in the
 * firmware repo. This is the canonical list every dashboard ships
 * with; adding a preset here without adding one there means the
 * sandbox wizard drops users into a layout that won't exist on
 * real hardware. */
typedef struct { const char *make; const char *version; } preset_t;
static const preset_t PRESETS[] = {
    { "ECU Master", "Black / Classic"          },
    { "MegaSquirt", "MS3-Pro"                   },
    { "Haltech",    "Nexus / Elite"             },
    { "MaxxECU",    "firmware 1.2"              },
    { "MaxxECU",    "firmware 1.3+"             },
    { "Ford",       "Falcon BA / BF"            },
    { "Ford",       "Falcon FG"                 },
    { "Link ECU",   "G4+ / G4X Generic Dash"    },
    { "RDM-7",      "Internal (calculated gear)" },
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

/* Dashboard scene — renders the firmware's default layout via the
 * REAL widget code path (layout_manager_apply_json), then injects
 * mock signal values on a timer to simulate a drive cycle. The
 * pixels on screen are the same ones the device would draw; only
 * the CAN source is synthetic. */
static lv_obj_t *s_dash_screen   = NULL;
static lv_timer_t *s_dash_timer  = NULL;
static double   s_dash_started_ms = 0;

/* ── Scene identifiers exported to JS ─────────────────────────────────
 * Tour scripts use these to rewind the LVGL state when the user taps
 * Back. Keep in sync with src/tutorial-runner.ts SCENE_MAP. */
#define SCENE_STEP1           0  /* CAN scan running (fresh start)    */
#define SCENE_STEP1_DONE      1  /* CAN scan complete, Apply visible  */
#define SCENE_STEP2           2  /* ECU picker overlay                */
#define SCENE_STEP3           3  /* Connect Your Device               */
#define SCENE_WIFI_PICKER     4  /* WiFi scan list overlay            */
#define SCENE_WIFI_CONNECTED  5  /* WiFi list with "Connected" status */
#define SCENE_DASHBOARD       6  /* Default layout "driving" with mock data */
#define SCENE_DASHBOARD_MENU  7  /* Dashboard + Menu button revealed  */
#define SCENE_SETUP_MENU      8  /* Setup Menu modal (Layout/Splash)  */
#define SCENE_DEVICE_SETTINGS 9  /* Device Settings screen            */
#define SCENE_WIDGET_CONFIG  10  /* Widget config modal over dashboard */
#define SCENE_WIFI_SETTINGS  11  /* Multi-SSID Wi-Fi Settings screen   */
#define SCENE_PEAKS          12  /* Signal peaks / min table           */
#define SCENE_DIAGNOSTICS    13  /* System health / diagnostics        */

/* External hooks (src/c/menu_sandbox.c, src/c/device_settings_sandbox.c,
 * src/c/widget_config_sandbox.c, src/c/extra_screens_sandbox.c). */
extern void menu_sandbox_attach_dashboard(lv_obj_t *dash_screen);
extern void menu_sandbox_show_menu_button(void);
extern void menu_sandbox_hide_menu_button(void);
extern void menu_sandbox_open_setup_menu(void);
extern void menu_sandbox_close_setup_menu(void);
extern void device_settings_sandbox_open(lv_obj_t *return_screen);
extern void device_settings_sandbox_close(void);
extern void widget_config_sandbox_open(lv_obj_t *return_screen);
extern void widget_config_sandbox_close(void);
extern void wifi_settings_sandbox_open(lv_obj_t *return_screen);
extern void wifi_settings_sandbox_close(void);
extern void peaks_sandbox_open(lv_obj_t *return_screen);
extern void peaks_sandbox_close(void);
extern void diagnostics_sandbox_open(lv_obj_t *return_screen);
extern void diagnostics_sandbox_close(void);

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
    /* Firmware ui_ecu_picker.c _style_dropdown exactly. */
    lv_obj_set_style_bg_color(s_ecu_make_dd, lv_color_hex(0x181C18), 0);
    lv_obj_set_style_bg_opa(s_ecu_make_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_ecu_make_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_ecu_make_dd, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(s_ecu_make_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ecu_make_dd, 1, 0);
    lv_obj_set_style_radius(s_ecu_make_dd, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(s_ecu_make_dd, 8, 0);
    lv_obj_add_event_cb(s_ecu_make_dd, _ecu_make_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    /* Default to Haltech — by far the most common ECU for RDM-7 customers.
     * Index matches the position of Haltech in the deduped makes list
     * (ECU Master, MegaSquirt, Haltech, MaxxECU, Ford, Link ECU, RDM-7). */
    lv_dropdown_set_selected(s_ecu_make_dd, 2);

    /* Version label + dropdown */
    lv_obj_t *ver_lbl = lv_label_create(card);
    lv_label_set_text(ver_lbl, "Version");
    lv_obj_align(ver_lbl, LV_ALIGN_TOP_LEFT, 300, 74);
    lv_obj_set_style_text_font(ver_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ver_lbl, THEME_COLOR_TEXT_MUTED, 0);

    s_ecu_ver_dd = lv_dropdown_create(card);
    lv_obj_align(s_ecu_ver_dd, LV_ALIGN_TOP_LEFT, 300, 96);
    lv_obj_set_size(s_ecu_ver_dd, 260, 40);
    lv_obj_set_style_bg_color(s_ecu_ver_dd, lv_color_hex(0x181C18), 0);
    lv_obj_set_style_bg_opa(s_ecu_ver_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_ecu_ver_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_ecu_ver_dd, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(s_ecu_ver_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_ecu_ver_dd, 1, 0);
    lv_obj_set_style_radius(s_ecu_ver_dd, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(s_ecu_ver_dd, 8, 0);

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

/* Faithful clone of main/ui/screens/ui_wifi.c's wifi_ui_show.
 *
 * Dimensions, fonts, and colours taken verbatim from the firmware:
 *   main container 780 x 460, header 780 x 44, body 780 x 416,
 *   left panel 350 x 396 (Controls card — Mode dropdown, AP info,
 *   Start-on-Boot dropdown), right panel 390 x 396 (AVAILABLE
 *   NETWORKS list + Scan button).
 *
 * The "Join a WiFi Network" button in the wizard's Step 3 jumps
 * here, matching the real firmware's wizard_btn_wifi_join_cb
 * behaviour of opening the full ui_wifi screen. */
static void _show_wifi_picker(void) {
    s_wifi_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wifi_overlay);
    lv_obj_set_size(s_wifi_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(s_wifi_overlay);
    lv_obj_set_style_bg_color(s_wifi_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_wifi_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_wifi_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Main container 780x460, centred ───────────────────────────── */
    lv_obj_t *main_cont = lv_obj_create(s_wifi_overlay);
    lv_obj_set_size(main_cont, 780, 460);
    lv_obj_center(main_cont);
    lv_obj_set_style_bg_color(main_cont, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(main_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(main_cont, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(main_cont, 1, 0);
    lv_obj_set_style_radius(main_cont, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(main_cont, 0, 0);
    lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header 780x44: Back, "Wi-Fi Settings", status ─────────────── */
    lv_obj_t *header = lv_obj_create(main_cont);
    lv_obj_set_size(header, 780, 44);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, 70, 30);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(back, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, 2, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, _wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(bl);
    lv_obj_set_style_text_font(bl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bl, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Wi-Fi Settings");
    lv_obj_center(title);
    lv_obj_set_style_text_font(title, THEME_FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    /* ── Body 780x416: 2-column split (10 px pad) ─────────────────── */
    lv_obj_t *body = lv_obj_create(main_cont);
    lv_obj_set_size(body, 780, 416);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Left panel: Controls card (Mode / Boot dropdowns). */
    lv_obj_t *left = lv_obj_create(body);
    lv_obj_set_size(left, 350, 396);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x393C39), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_radius(left, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(left, 12, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ctrl_title = lv_label_create(left);
    lv_label_set_text(ctrl_title, "CONTROLS");
    lv_obj_align(ctrl_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(ctrl_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(ctrl_title, THEME_COLOR_TEXT_MUTED, 0);

    /* Mode row */
    lv_obj_t *mode_lbl = lv_label_create(left);
    lv_label_set_text(mode_lbl, "Mode");
    lv_obj_align(mode_lbl, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_set_style_text_font(mode_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(mode_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *mode_dd = lv_dropdown_create(left);
    lv_dropdown_set_options_static(mode_dd, "Off\nWiFi (Client)\nHotspot (AP)");
    lv_dropdown_set_selected(mode_dd, 1);
    lv_obj_set_size(mode_dd, 170, 30);
    lv_obj_align(mode_dd, LV_ALIGN_TOP_RIGHT, 0, 22);
    lv_obj_set_style_bg_color(mode_dd, lv_color_hex(0x181C18), 0);
    lv_obj_set_style_bg_opa(mode_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mode_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(mode_dd, 1, 0);
    lv_obj_set_style_radius(mode_dd, 2, 0);
    lv_obj_set_style_text_color(mode_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(mode_dd, THEME_FONT_SMALL, 0);

    /* AP info rows (visible when Hotspot selected — stays shown here
     * for demo clarity, matching how the firmware renders when the
     * user initially lands on the screen in AP-only mode). */
    static const struct { const char *k, *v; } ROWS[] = {
        { "SSID:",              "RDM7-XXXX"       },
        { "IP:",                "192.168.4.1"     },
        { "Hotspot Password:",  "rdm7dash"        },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(left);
        lv_obj_set_size(row, 330, 24);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 70 + i * 28);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *k = lv_label_create(row);
        lv_label_set_text(k, ROWS[i].k);
        lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_font(k, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(k, THEME_COLOR_TEXT_MUTED, 0);

        lv_obj_t *v = lv_label_create(row);
        lv_label_set_text(v, ROWS[i].v);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_font(v, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(v, THEME_COLOR_TEXT_PRIMARY, 0);
    }

    /* Boot row (Start on Boot dropdown) */
    lv_obj_t *boot_lbl = lv_label_create(left);
    lv_label_set_text(boot_lbl, "Start on Boot");
    lv_obj_align(boot_lbl, LV_ALIGN_TOP_LEFT, 0, 180);
    lv_obj_set_style_text_font(boot_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(boot_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *boot_dd = lv_dropdown_create(left);
    lv_dropdown_set_options_static(boot_dd, "Off\nWiFi (Client)\nHotspot (AP)");
    lv_dropdown_set_selected(boot_dd, 1);
    lv_obj_set_size(boot_dd, 170, 30);
    lv_obj_align(boot_dd, LV_ALIGN_TOP_RIGHT, 0, 176);
    lv_obj_set_style_bg_color(boot_dd, lv_color_hex(0x181C18), 0);
    lv_obj_set_style_bg_opa(boot_dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(boot_dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(boot_dd, 1, 0);
    lv_obj_set_style_radius(boot_dd, 2, 0);
    lv_obj_set_style_text_color(boot_dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(boot_dd, THEME_FONT_SMALL, 0);

    /* Right panel: AVAILABLE NETWORKS card. */
    lv_obj_t *right = lv_obj_create(body);
    lv_obj_set_size(right, 390, 396);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x393C39), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_radius(right, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(right, 12, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scan_title = lv_label_create(right);
    lv_label_set_text(scan_title, "AVAILABLE NETWORKS");
    lv_obj_align(scan_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(scan_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(scan_title, THEME_COLOR_TEXT_MUTED, 0);

    /* Status + list */
    s_wifi_status = lv_label_create(right);
    lv_label_set_text(s_wifi_status, "Tap a network to connect");
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_text_font(s_wifi_status, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_wifi_status, THEME_COLOR_TEXT_MUTED, 0);

    s_wifi_list = lv_obj_create(right);
    lv_obj_set_size(s_wifi_list, 366, 294);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_LEFT, 0, 38);
    lv_obj_set_style_bg_color(s_wifi_list, lv_color_hex(0x393C39), 0);
    lv_obj_set_style_bg_opa(s_wifi_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_obj_set_style_radius(s_wifi_list, 2, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 2, 0);
    lv_obj_set_style_pad_gap(s_wifi_list, 2, 0);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < MOCK_AP_COUNT; i++) {
        lv_obj_t *row = lv_btn_create(s_wifi_list);
        lv_obj_set_size(row, lv_pct(100), 40);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x292C29), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_user_data(row, (void *)MOCK_APS[i].ssid);
        lv_obj_add_event_cb(row, _wifi_ap_clicked_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, MOCK_APS[i].ssid);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 0, -7);
        lv_obj_set_style_text_font(ssid_lbl, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(ssid_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

        lv_obj_t *meta = lv_label_create(row);
        lv_label_set_text_fmt(meta, "%s  %d dBm",
                              MOCK_APS[i].secured ? LV_SYMBOL_WIFI : "open",
                              MOCK_APS[i].rssi);
        lv_obj_align(meta, LV_ALIGN_LEFT_MID, 0, 8);
        lv_obj_set_style_text_font(meta, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(meta, THEME_COLOR_TEXT_MUTED, 0);

        lv_obj_t *bars = lv_label_create(row);
        lv_label_set_text_fmt(bars, "%d/4", _rssi_bars(MOCK_APS[i].rssi));
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_font(bars, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(bars, THEME_COLOR_TEXT_MUTED, 0);
    }

    /* Scan button at bottom-right of right panel. */
    lv_obj_t *scan_btn = lv_btn_create(right);
    lv_obj_set_size(scan_btn, 140, 32);
    lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_bg_opa(scan_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    lv_obj_set_style_radius(scan_btn, 2, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);

    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan Again");
    lv_obj_center(scan_lbl);
    lv_obj_set_style_text_font(scan_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(scan_lbl, lv_color_white(), 0);
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
 * Renders the firmware's default layout via layout_manager_apply_json,
 * then feeds synthetic signal values through signal_inject_test_value
 * on a 60 ms timer. Because we're going through the exact widget
 * creation path the device uses, every pixel on the canvas matches
 * what the hardware would render given the same layout + signal data.
 */

/* Firmware glue — pulled from widgets/ and layout/. */
extern esp_err_t layout_manager_apply_json(cJSON *root, lv_obj_t *screen);
extern void      signal_registry_reset(void);
extern void      signal_registry_init(void);
extern void      signal_inject_test_value(const char *name, float v);
extern void      widget_registry_reset(void);
extern void      font_manager_reset_instances(void);
extern void      font_manager_init(void);
/* Generate the firmware's canonical default layout in memory.
 * Returns a cJSON root the caller must cJSON_Delete. */
extern cJSON    *dashboard_build_default_layout_root(void);

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

/* Freeze flag: when true, _dash_tick injects zeros for every signal
 * instead of running the drive-cycle simulation. The host page sets
 * this via sandbox_set_dashboard_frozen() so the dash can show its
 * default layout in a "no CAN bus / engine off" state on initial paint
 * (and any time the visitor scrolls back into the hero region).
 *
 * Defaults to true so the very first frame after WASM init renders
 * with all zeros — there's no race where the host has to win a
 * setter call before the LVGL timer fires its first tick. */
static bool s_dash_frozen = true;

/* Force-stale all signals immediately (no waiting on the 2-second
 * timeout). Widget renderers fall back to their "---" placeholder
 * whenever a signal is stale, which is exactly the no-CAN-bus look
 * we want during the hero / idle region. */
extern void signal_mark_all_stale(void);

EMSCRIPTEN_KEEPALIVE
void sandbox_set_dashboard_frozen(int frozen) {
    s_dash_frozen = (frozen != 0);
    if (s_dash_frozen) signal_mark_all_stale();
}

static void _dash_tick(lv_timer_t *t) {
    (void)t;
    if (!s_dash_screen || !lv_obj_is_valid(s_dash_screen)) return;

    /* Frozen mode: skip the entire drive-cycle. Signals stay stale
     * (set by sandbox_set_dashboard_frozen via signal_mark_all_stale)
     * and every widget falls through to its "---" placeholder. */
    if (s_dash_frozen) return;

    double ms = emscripten_get_now() - s_dash_started_ms;

    float rpm, speed, throttle;
    int gear;
    _drive_state(ms, &rpm, &speed, &throttle, &gear);

    /* Feed the synthetic values through the real signal pipeline.
     * Every widget subscribed to these signal names in the default
     * layout's signals[] array will redraw via its normal update
     * callback — no hand-wiring, no special-case code. */
    signal_inject_test_value("RPM",             rpm);
    signal_inject_test_value("VEHICLE_SPEED",   speed);
    signal_inject_test_value("THROTTLE",        throttle);
    signal_inject_test_value("GEAR",            (float)gear);
    signal_inject_test_value("COOLANT_TEMP",    82.0f + (float)fmin(ms / 40000.0, 5.0));
    signal_inject_test_value("INTAKE_AIR_TEMP", _wave(ms, 34.0f, 6.0f,  4200.0, 0));
    signal_inject_test_value("LAMBDA",          _wave(ms,  0.92f, 0.08f, 3100.0, 800));
    signal_inject_test_value("OIL_TEMP",        _wave(ms, 95.0f, 5.0f,  8000.0, 1500));
    signal_inject_test_value("OIL_PRESSURE",    _wave(ms, 380.0f, 70.0f, 2400.0, 400));
    signal_inject_test_value("MAP",             _wave(ms, 95.0f, 40.0f, 2000.0, 1100));
    signal_inject_test_value("IGNITION",        _wave(ms, 18.0f, 6.0f,  2800.0, 2300));
    signal_inject_test_value("FUEL_TRIM",       _wave(ms,  1.5f,  2.0f, 5500.0, 3000));
    signal_inject_test_value("BATTERY_VOLTAGE", _wave(ms, 13.8f,  0.3f, 9000.0, 0));
    signal_inject_test_value("FUEL_PRESSURE",   _wave(ms, 48.0f,  2.0f, 3600.0, 2000));
}

/* Build the dashboard ONCE on the active screen. Idempotent — repeat
 * calls during scene transitions are no-ops. The drive cycle timer
 * keeps ticking the entire session, so widgets behind the wizard
 * overlay are still drawing live data (matches firmware behaviour:
 * the wizard sits on top of an already-running default layout). */
static void _ensure_dashboard_loaded(void) {
    if (s_dash_screen && lv_obj_is_valid(s_dash_screen)) return;

    s_dash_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_dash_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dash_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dash_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(s_dash_screen);

    widget_registry_reset();
    signal_registry_reset();
    font_manager_reset_instances();
    font_manager_init();
    signal_registry_init();

    cJSON *root = dashboard_build_default_layout_root();
    if (root) {
        esp_err_t err = layout_manager_apply_json(root, s_dash_screen);
        cJSON_Delete(root);
        if (err != ESP_OK) {
            printf("[dash] layout_manager_apply_json failed: %d\n", (int)err);
        }
    } else {
        printf("[dash] dashboard_build_default_layout_root returned NULL\n");
    }

    /* Synthetic CAN drive cycle starts immediately and runs for the
     * lifetime of the WASM module — even while the wizard overlay
     * obscures the dashboard. */
    s_dash_started_ms = emscripten_get_now();
    if (s_dash_timer) lv_timer_del(s_dash_timer);
    s_dash_timer = lv_timer_create(_dash_tick, 60, NULL);

    /* Hand the freshly-built dashboard over to menu_sandbox so the
     * tap-to-reveal Menu button and its auto-hide timer attach to the
     * right screen. Idempotent — safe to call whenever the dashboard
     * is rebuilt. */
    menu_sandbox_attach_dashboard(s_dash_screen);
}

/* Expose the dashboard screen to sibling modules (menu_sandbox,
 * device_settings_sandbox) without making them depend on wizard state.
 * Lazy-init so the first caller doesn't have to pre-load the wizard. */
lv_obj_t *wizard_sandbox_get_dashboard_screen(void) {
    _ensure_dashboard_loaded();
    return s_dash_screen;
}

/* SCENE_DASHBOARD just hides the wizard overlay — the dashboard is
 * already there, drawing under it. */
static void _show_dashboard(void) {
    _ensure_dashboard_loaded();
    if (s_overlay && lv_obj_is_valid(s_overlay)) {
        lv_obj_del(s_overlay);
    }
    s_overlay = s_card = s_step1 = s_step3 = NULL;
    s_scan_status = s_scan_progress = s_scan_bar = s_scan_detail = NULL;
    s_btn_apply = s_btn_next1 = s_btn_cancel = NULL;
    for (int i = 0; i < 4; i++) s_scan_results[i] = NULL;
    if (s_ecu_overlay && lv_obj_is_valid(s_ecu_overlay))   lv_obj_del(s_ecu_overlay);
    if (s_wifi_overlay && lv_obj_is_valid(s_wifi_overlay)) lv_obj_del(s_wifi_overlay);
    s_ecu_overlay = s_ecu_make_dd = s_ecu_ver_dd = s_ecu_apply = NULL;
    s_wifi_overlay = s_wifi_status = s_wifi_list = NULL;
}

/* ── Teardown / public entry ─────────────────────────────────────────── */

/* Skip / Finish from the wizard. Drops the overlay so the dashboard
 * (which has been running in the background the whole time) becomes
 * visible. No "Setup Complete" splash — the device doesn't have one
 * either; once first_run_done is set, you're just at the dashboard. */
static void _close_wizard(void) {
    if (s_scan_timer)         { lv_timer_del(s_scan_timer);         s_scan_timer         = NULL; }
    if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
    _wifi_close();
    _ecu_close();
    if (s_overlay && lv_obj_is_valid(s_overlay)) lv_obj_del(s_overlay);
    s_overlay = s_card = s_step1 = s_step3 = NULL;
    s_scan_status = s_scan_progress = s_scan_bar = s_scan_detail = NULL;
    s_btn_apply = s_btn_next1 = s_btn_cancel = NULL;
    for (int i = 0; i < 4; i++) s_scan_results[i] = NULL;
}

/* Rebuild the wizard overlay + card on top of the dashboard. Called
 * on every scene transition (Play, Next, Back, jump-to-step) so each
 * scene starts from a guaranteed-clean state. Crucially: this does
 * NOT touch the dashboard screen / widgets / drive timer — those live
 * underneath the wizard for the whole session, exactly like firmware. */
static void _rebuild_base_overlay(void) {
    /* Make sure the dashboard exists and is the active screen. */
    _ensure_dashboard_loaded();

    /* Kill the floating Menu button + its auto-hide timer so it doesn't
     * peek through the wizard's semi-transparent backdrop. Previously a
     * dashboard-scene tap (or leftover state from a prior scene) could
     * leave the button visible while the wizard ran over the top. */
    menu_sandbox_hide_menu_button();

    /* Tear down only wizard-side state. */
    if (s_scan_timer)         { lv_timer_del(s_scan_timer);         s_scan_timer         = NULL; }
    if (s_wifi_connect_timer) { lv_timer_del(s_wifi_connect_timer); s_wifi_connect_timer = NULL; }
    if (s_ecu_overlay && lv_obj_is_valid(s_ecu_overlay))   lv_obj_del(s_ecu_overlay);
    if (s_wifi_overlay && lv_obj_is_valid(s_wifi_overlay)) lv_obj_del(s_wifi_overlay);
    s_ecu_overlay = s_ecu_make_dd = s_ecu_ver_dd = s_ecu_apply = NULL;
    s_wifi_overlay = s_wifi_status = s_wifi_list = NULL;

    if (s_overlay && lv_obj_is_valid(s_overlay)) lv_obj_del(s_overlay);

    /* Build the dimming overlay + card on the dashboard screen. The
     * 80% black backdrop lets the running dashboard show through
     * faintly, matching the firmware's first-run wizard layering. */
    s_overlay = lv_obj_create(s_dash_screen);
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
    /* Post-wizard scenes live on separate screens (dashboard, setup
     * menu, device settings). Close any wizard chrome AND tear down
     * any Device Settings / Setup Menu screens that were up from a
     * previous scene jump before switching. */
    if (scene == SCENE_DASHBOARD ||
        scene == SCENE_DASHBOARD_MENU ||
        scene == SCENE_SETUP_MENU ||
        scene == SCENE_DEVICE_SETTINGS ||
        scene == SCENE_WIDGET_CONFIG ||
        scene == SCENE_WIFI_SETTINGS ||
        scene == SCENE_PEAKS ||
        scene == SCENE_DIAGNOSTICS) {
        /* Drop wizard overlay if any. */
        if (s_overlay && lv_obj_is_valid(s_overlay)) {
            lv_obj_del(s_overlay);
            s_overlay = s_card = s_step1 = s_step3 = NULL;
        }
        if (s_ecu_overlay && lv_obj_is_valid(s_ecu_overlay))   lv_obj_del(s_ecu_overlay);
        if (s_wifi_overlay && lv_obj_is_valid(s_wifi_overlay)) lv_obj_del(s_wifi_overlay);
        s_ecu_overlay = s_ecu_make_dd = s_ecu_ver_dd = s_ecu_apply = NULL;
        s_wifi_overlay = s_wifi_status = s_wifi_list = NULL;
        _ensure_dashboard_loaded();
        lv_scr_load(s_dash_screen);
        /* Force dashboard to be the active screen before we layer
         * setup menu / device settings screens on top. */
        menu_sandbox_close_setup_menu();
        device_settings_sandbox_close();
        widget_config_sandbox_close();
        wifi_settings_sandbox_close();
        peaks_sandbox_close();
        diagnostics_sandbox_close();
        menu_sandbox_hide_menu_button();

        if (scene == SCENE_DASHBOARD_MENU) {
            menu_sandbox_show_menu_button();
        } else if (scene == SCENE_SETUP_MENU) {
            menu_sandbox_open_setup_menu();
        } else if (scene == SCENE_DEVICE_SETTINGS) {
            device_settings_sandbox_open(s_dash_screen);
        } else if (scene == SCENE_WIDGET_CONFIG) {
            widget_config_sandbox_open(s_dash_screen);
        } else if (scene == SCENE_WIFI_SETTINGS) {
            wifi_settings_sandbox_open(s_dash_screen);
        } else if (scene == SCENE_PEAKS) {
            peaks_sandbox_open(s_dash_screen);
        } else if (scene == SCENE_DIAGNOSTICS) {
            diagnostics_sandbox_open(s_dash_screen);
        }
        return;
    }

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
