/* device_settings_sandbox.c — Device Settings screen clone.
 *
 * Mirrors the firmware's device_settings_with_return_screen() layout
 * widget-for-widget and section-for-section:
 *
 *   Row 1 (160):  CAN BUS           | DEVICE INFO
 *   Row 2 (260):  NETWORK & UPDATES | DISPLAY
 *   Row 3 (95):   DATA LOGGING | PEAK HOLD | TESTING
 *   Full-width CAN BUS DIAGNOSTICS
 *   System Diagnostics / Run Setup Wizard / Reset to Default
 *
 * Interactions that actually do something in the sandbox:
 *   - Brightness slider → live canvas-level opacity via CSS filter
 *     (caller exposes sandbox_set_display_brightness from JS)
 *   - Rotation (via direct LVGL API; see _rotation_btn_cb)
 *   - Night mode toggle (UI-only label flip)
 *   - Sim toggle (UI-only — the dash is already mock-driven)
 *   - Data logger start/stop + rate dropdown (UI-only)
 *   - Bitrate / ECU dropdowns (UI-only)
 *   - Close button returns to the stored return_screen (dashboard)
 *
 * Interactions that open a placeholder dialog (so taps don't feel dead):
 *   - Wi-Fi Settings, Show QR, Check Updates, Run Bus Scan,
 *     Dimmer Switch Config, ECU Type, View Peaks, System Diagnostics,
 *     Run Setup Wizard, Factory Reset.
 *
 * All visuals (colours, pad, fonts, radii, sizes) pulled verbatim from
 * main/ui/settings/device_settings.c so the sandbox is indistinguishable
 * from the real screen at this zoom level. */

#include "lvgl.h"
#include "esp_idf_shim.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Theme (kept in sync with the other sandbox modules) ─────────────── */
#define THEME_COLOR_BG                  lv_color_hex(0x000000)
#define THEME_COLOR_SURFACE             lv_color_hex(0x292C29)
#define THEME_COLOR_PANEL               lv_color_hex(0x393C39)
#define THEME_COLOR_SECTION_BG          lv_color_hex(0x393C39)
#define THEME_COLOR_INPUT_BG            lv_color_hex(0x181C18)
#define THEME_COLOR_BORDER              lv_color_hex(0x181C18)
#define THEME_COLOR_SCROLLBAR           lv_color_hex(0x4A4D4A)
#define THEME_COLOR_TEXT_PRIMARY        lv_color_hex(0xE8E8E8)
#define THEME_COLOR_TEXT_MUTED          lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_HINT           lv_color_hex(0x606260)
#define THEME_COLOR_TEXT_ON_ACCENT      lv_color_hex(0xFFFFFF)
#define THEME_COLOR_ACCENT_BLUE         lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_BLUE_PRESSED lv_color_hex(0x1976D2)
#define THEME_COLOR_ACCENT_TEAL         lv_color_hex(0x26C6DA)
#define THEME_COLOR_BTN_SAVE            lv_color_hex(0x2196F3)
#define THEME_COLOR_STATUS_CONNECTED    lv_color_hex(0x4CAF50)
#define THEME_COLOR_STATUS_WARN         lv_color_hex(0xFFC107)
#define THEME_COLOR_STATUS_ERROR        lv_color_hex(0xF84040)
#define THEME_COLOR_SECTION_CAN_TITLE   lv_color_hex(0x26C6DA)
#define THEME_FONT_TINY                 (&lv_font_montserrat_10)
#define THEME_FONT_SMALL                (&lv_font_montserrat_12)
#define THEME_FONT_MEDIUM               (&lv_font_montserrat_14)
#define THEME_FONT_LARGE                (&lv_font_montserrat_18)
#define THEME_RADIUS_SMALL              2
#define THEME_RADIUS_NORMAL             4
#define THEME_RADIUS_PILL               20
#define THEME_PAD_SMALL                 6
#define THEME_PAD_NORMAL                10

/* Live secondary screens — implemented in extra_screens_sandbox.c. */
extern void wifi_settings_sandbox_open(lv_obj_t *return_screen);
extern void peaks_sandbox_open(lv_obj_t *return_screen);
extern void diagnostics_sandbox_open(lv_obj_t *return_screen);
extern void dimmer_config_sandbox_open(void);
extern void bus_scan_sandbox_open(void);

/* Wizard — relaunches the first-run flow. Defined in wizard_sandbox.c
 * (EMSCRIPTEN_KEEPALIVE-exported so it's also reachable from JS). */
extern void sandbox_set_scene(int scene);
#define SCENE_STEP1_ID 0

/* Live display dimming — dims the <canvas> via CSS filter so the user
 * sees the slider actually affecting screen brightness, not just the
 * %-label. LVGL can't dim its own output, but filtering the element
 * we render into gets us the same visible result. */
EM_JS(void, sandbox_js_set_brightness, (int pct), {
    var val = Math.max(5, Math.min(100, pct)) / 100.0;
    var cs = document.getElementsByTagName('canvas');
    for (var i = 0; i < cs.length; i++) {
        cs[i].style.filter = 'brightness(' + val + ')';
    }
});

/* ── Module state ────────────────────────────────────────────────────── */
static lv_obj_t *s_settings_screen = NULL;
static lv_obj_t *s_return_screen   = NULL;
/* The scrollable content column — tours call sandbox_device_settings_scroll()
 * to jump the viewport to a specific section while a caption explains it. */
static lv_obj_t *s_content_col     = NULL;

/* Live labels — we update these in response to events / timers */
static lv_obj_t *s_brightness_val_label = NULL;
static lv_obj_t *s_rotation_btn_label   = NULL;
static lv_obj_t *s_night_btn_label      = NULL;
static lv_obj_t *s_sim_btn_label        = NULL;
static lv_obj_t *s_log_btn_label        = NULL;
static lv_obj_t *s_log_status_label     = NULL;
static lv_obj_t *s_wifi_status_label    = NULL;
static lv_obj_t *s_web_status_label     = NULL;
static lv_obj_t *s_ap_status_label      = NULL;
static lv_obj_t *s_can_health_dot       = NULL;
static lv_obj_t *s_can_health_label     = NULL;
static lv_obj_t *s_can_summary_label    = NULL;
static lv_obj_t *s_can_details_toggle   = NULL;
static lv_obj_t *s_can_details_grid     = NULL;
static lv_obj_t *s_can_detail_labels[6] = {NULL};

/* State tracked in memory (not persisted — refreshes on reopen) */
static uint8_t s_brightness_pct  = 80;
static uint8_t s_rotation_deg    = 0;
static bool    s_night_mode_on   = false;
static bool    s_sim_active      = true;   /* dashboard runs mock data */
static bool    s_log_active      = false;
static bool    s_details_open    = false;
static uint32_t s_rx_frame_count = 0;

/* ─────────────────────────────────────────────────────────────────────
 *  Small helpers — placeholder toast + dropdown styling
 * ───────────────────────────────────────────────────────────────────── */

static void _toast_delete_cb(lv_timer_t *t) {
    lv_obj_t *lbl = (lv_obj_t *)t->user_data;
    if (lbl && lv_obj_is_valid(lbl)) lv_obj_del(lbl);
}

/* Brief bottom-centre toast for placeholder actions so taps feel live. */
static void _toast(const char *text) {
    if (!s_settings_screen || !lv_obj_is_valid(s_settings_screen)) return;
    lv_obj_t *lbl = lv_label_create(s_settings_screen);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, THEME_COLOR_ACCENT_TEAL, 0);
    lv_obj_set_style_bg_color(lbl, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(lbl, 12, 0);
    lv_obj_set_style_pad_ver(lbl, 6, 0);
    lv_obj_set_style_radius(lbl, THEME_RADIUS_NORMAL, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_move_foreground(lbl);
    lv_timer_t *t = lv_timer_create(_toast_delete_cb, 1600, lbl);
    lv_timer_set_repeat_count(t, 1);
}

static void _style_dropdown(lv_obj_t *dd) {
    lv_obj_set_style_bg_color(dd, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(dd, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(dd, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(dd, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(dd, 4, 0);
    lv_obj_set_style_text_color(dd, THEME_COLOR_TEXT_MUTED, LV_PART_INDICATOR);
}

static void _style_secondary_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void _style_primary_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_color(btn, THEME_COLOR_ACCENT_BLUE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
}

/* ─────────────────────────────────────────────────────────────────────
 *  Event callbacks
 * ───────────────────────────────────────────────────────────────────── */

static void _close_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *scr = s_settings_screen;
    lv_obj_t *ret = s_return_screen;
    s_settings_screen = NULL;
    /* Null every live-widget pointer now; the screen is about to die. */
    s_brightness_val_label = s_rotation_btn_label = s_night_btn_label = NULL;
    s_sim_btn_label = s_log_btn_label = s_log_status_label = NULL;
    s_wifi_status_label = s_web_status_label = s_ap_status_label = NULL;
    s_can_health_dot = s_can_health_label = s_can_summary_label = NULL;
    s_can_details_toggle = s_can_details_grid = NULL;
    for (int i = 0; i < 6; i++) s_can_detail_labels[i] = NULL;
    if (ret && lv_obj_is_valid(ret)) lv_scr_load(ret);
    if (scr && lv_obj_is_valid(scr)) lv_obj_del_async(scr);
}

static void _brightness_slider_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    if (v < 5) v = 5;
    s_brightness_pct = (uint8_t)v;
    if (s_brightness_val_label && lv_obj_is_valid(s_brightness_val_label))
        lv_label_set_text_fmt(s_brightness_val_label, "%d%%", v);
    sandbox_js_set_brightness(v);
}

static void _rotation_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_rotation_deg = (uint8_t)((s_rotation_deg + 1) % 4);
    lv_disp_t *d = lv_disp_get_default();
    if (d) lv_disp_set_rotation(d, (lv_disp_rot_t)s_rotation_deg);
    if (s_rotation_btn_label && lv_obj_is_valid(s_rotation_btn_label)) {
        static const char *names[] = {
            "Rotation: 0\xC2\xB0",
            "Rotation: 90\xC2\xB0",
            "Rotation: 180\xC2\xB0",
            "Rotation: 270\xC2\xB0",
        };
        lv_label_set_text(s_rotation_btn_label, names[s_rotation_deg]);
    }
}

static void _night_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_night_mode_on = !s_night_mode_on;
    if (s_night_btn_label && lv_obj_is_valid(s_night_btn_label))
        lv_label_set_text(s_night_btn_label,
            s_night_mode_on ? "Night Mode: ON" : "Night Mode: OFF");
}

static void _sim_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_sim_active = !s_sim_active;
    if (s_sim_btn_label && lv_obj_is_valid(s_sim_btn_label)) {
        lv_label_set_text(s_sim_btn_label, s_sim_active ? "Sim: ON" : "Sim: OFF");
        lv_obj_set_style_text_color(s_sim_btn_label,
            s_sim_active ? THEME_COLOR_STATUS_CONNECTED : THEME_COLOR_TEXT_MUTED, 0);
    }
}

static void _log_toggle_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_log_active = !s_log_active;
    if (s_log_btn_label && lv_obj_is_valid(s_log_btn_label))
        lv_label_set_text(s_log_btn_label, s_log_active ? "Stop Logging" : "Start Logging");
    if (s_log_status_label && lv_obj_is_valid(s_log_status_label)) {
        lv_label_set_text(s_log_status_label, s_log_active ? "Logging" : "Stopped");
        lv_obj_set_style_text_color(s_log_status_label,
            s_log_active ? THEME_COLOR_STATUS_CONNECTED : THEME_COLOR_TEXT_MUTED, 0);
    }
}

static void _dropdown_noop_cb(lv_event_t *e) { (void)e; }

/* Launchers — hop out to a full-screen sibling that uses this screen
 * as the return target. On close the child script reloads s_settings
 * so the scroll position and live-widget references stay consistent. */
static void _open_wifi_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wifi_settings_sandbox_open(s_settings_screen);
}

static void _open_peaks_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    peaks_sandbox_open(s_settings_screen);
}

static void _open_diag_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    diagnostics_sandbox_open(s_settings_screen);
}

static void _open_dimmer_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* Dimmer popup layers on top of Device Settings rather than
     * replacing it — the overlay's click handler closes it so we
     * drop back here. */
    dimmer_config_sandbox_open();
}

static void _open_bus_scan_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    bus_scan_sandbox_open();
}

/* "Run Setup Wizard" — closes this settings screen and rebuilds the
 * wizard overlay on top of the dashboard. sandbox_set_scene handles
 * the teardown + re-open atomically. */
static void _run_wizard_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    sandbox_set_scene(SCENE_STEP1_ID);
}

/* Placeholder clicks for subsystems we don't emulate yet. */
static void _placeholder_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *text = (const char *)lv_event_get_user_data(e);
    _toast(text ? text : "Not available in sandbox");
}

static void _details_toggle_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_details_open = !s_details_open;
    if (s_can_details_toggle && lv_obj_is_valid(s_can_details_toggle))
        lv_label_set_text(s_can_details_toggle,
            s_details_open ? LV_SYMBOL_DOWN " Hide Details"
                           : LV_SYMBOL_RIGHT " Show Details");
    if (s_can_details_grid && lv_obj_is_valid(s_can_details_grid)) {
        if (s_details_open) lv_obj_clear_flag(s_can_details_grid, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(s_can_details_grid, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Periodic: refresh the CAN diagnostics numbers (fake but plausible). */
static void _can_diag_tick(lv_timer_t *t) {
    (void)t;
    if (!s_can_health_dot || !lv_obj_is_valid(s_can_health_dot)) return;
    /* Mock: one rx frame every ~8 ms when sim is on. */
    uint32_t rate = s_sim_active ? 142 : 0;
    s_rx_frame_count += rate;

    lv_obj_set_style_bg_color(s_can_health_dot,
        s_sim_active ? THEME_COLOR_STATUS_CONNECTED : THEME_COLOR_STATUS_ERROR, 0);
    if (s_can_health_label && lv_obj_is_valid(s_can_health_label))
        lv_label_set_text(s_can_health_label,
            s_sim_active ? "OK - receiving 142 fps" : "Silent - no CAN traffic");

    if (s_can_summary_label && lv_obj_is_valid(s_can_summary_label))
        lv_label_set_text_fmt(s_can_summary_label,
            "500 kbps - last ID 0x360 - %u fps", (unsigned)rate);

    if (s_can_detail_labels[0] && lv_obj_is_valid(s_can_detail_labels[0])) {
        lv_label_set_text_fmt(s_can_detail_labels[0], "RX Count: %u", (unsigned)s_rx_frame_count);
        lv_label_set_text    (s_can_detail_labels[1], "RX Errors: 0");
        lv_label_set_text    (s_can_detail_labels[2], "RX Missed: 0");
        lv_label_set_text    (s_can_detail_labels[3], "TX Count: 0");
        lv_label_set_text    (s_can_detail_labels[4], "TX Errors: 0");
        lv_label_set_text    (s_can_detail_labels[5], "Bus Errors: 0");
    }
}
static lv_timer_t *s_can_diag_timer = NULL;

/* ─────────────────────────────────────────────────────────────────────
 *  Screen builder
 * ───────────────────────────────────────────────────────────────────── */

static lv_obj_t *_make_section(lv_obj_t *parent) {
    lv_obj_t *s = lv_obj_create(parent);
    lv_obj_set_size(s, 0, lv_pct(100));
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_style_bg_color(s, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(s, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s, 1, 0);
    lv_obj_set_style_pad_all(s, 12, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t *_make_section_title(lv_obj_t *parent, const char *txt, lv_color_t color) {
    lv_obj_t *t = lv_label_create(parent);
    lv_label_set_text(t, txt);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(t, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(t, color, 0);
    lv_obj_set_style_text_letter_space(t, 1, 0);
    return t;
}

static lv_obj_t *_make_row(lv_obj_t *parent, int16_t height) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, lv_pct(100), height);
    lv_obj_set_style_bg_opa(r, 0, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_gap(r, 8, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    return r;
}

/* Wide single-section button used for the footer actions. */
static lv_obj_t *_footer_btn(lv_obj_t *parent, const char *text,
                             lv_color_t text_color,
                             lv_event_cb_t cb, void *user_data) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, lv_pct(100), 34);
    _style_secondary_btn(b);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, text_color, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

void device_settings_sandbox_open(lv_obj_t *return_screen) {
    s_return_screen = return_screen ? return_screen : lv_scr_act();

    s_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_settings_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_settings_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Main container ─────────────────────────────────────────── */
    lv_obj_t *main = lv_obj_create(s_settings_screen);
    lv_obj_set_size(main, 760, 440);
    lv_obj_center(main);
    lv_obj_set_style_bg_color(main, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(main, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(main, 1, 0);
    lv_obj_set_style_radius(main, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header bar ─────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(main);
    lv_obj_set_size(header, lv_pct(100), 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Device Settings");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 60, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    _style_secondary_btn(close_btn);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(close_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(close_btn, _close_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ── Scrollable content column ──────────────────────────────── */
    lv_obj_t *content = s_content_col = lv_obj_create(main);
    lv_obj_set_size(content, lv_pct(100), 392);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, THEME_PAD_NORMAL, 0);
    lv_obj_set_style_pad_right(content, 20, 0);
    lv_obj_set_style_pad_row(content, THEME_PAD_SMALL, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(content, THEME_COLOR_SCROLLBAR, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, 150, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, THEME_RADIUS_SMALL, LV_PART_SCROLLBAR);

    /* ── Row 1: CAN BUS | DEVICE INFO ────────────────────────────── */
    lv_obj_t *row1 = _make_row(content, 160);

    /* CAN BUS section */
    lv_obj_t *can_sec = _make_section(row1);
    _make_section_title(can_sec, "CAN BUS", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *bitrate_lbl = lv_label_create(can_sec);
    lv_label_set_text(bitrate_lbl, "Bitrate");
    lv_obj_align(bitrate_lbl, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_text_font(bitrate_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bitrate_lbl, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *bitrate_dd = lv_dropdown_create(can_sec);
    lv_dropdown_set_options(bitrate_dd, "125 kbps\n250 kbps\n500 kbps\n1 Mbps");
    lv_obj_set_size(bitrate_dd, 140, 32);
    lv_obj_align(bitrate_dd, LV_ALIGN_TOP_LEFT, 0, 42);
    _style_dropdown(bitrate_dd);
    lv_dropdown_set_selected(bitrate_dd, 2);  /* 500 kbps */
    lv_obj_add_event_cb(bitrate_dd, _dropdown_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *ecu_lbl = lv_label_create(can_sec);
    lv_label_set_text(ecu_lbl, "ECU Type");
    lv_obj_align(ecu_lbl, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_obj_set_style_text_font(ecu_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ecu_lbl, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *ecu_btn = lv_btn_create(can_sec);
    lv_obj_set_size(ecu_btn, lv_pct(62), 32);
    lv_obj_align(ecu_btn, LV_ALIGN_TOP_LEFT, 80, 86);
    _style_secondary_btn(ecu_btn);
    lv_obj_set_style_bg_color(ecu_btn, THEME_COLOR_INPUT_BG, 0);
    lv_obj_t *ecu_val = lv_label_create(ecu_btn);
    lv_label_set_text(ecu_val, "Haltech - Nexus / Elite");
    lv_label_set_long_mode(ecu_val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ecu_val, lv_pct(95));
    lv_obj_align(ecu_val, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(ecu_val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ecu_val, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(ecu_btn, _placeholder_cb, LV_EVENT_CLICKED, "ECU picker - preview");

    /* DEVICE INFO section */
    lv_obj_t *info_sec = _make_section(row1);
    _make_section_title(info_sec, "DEVICE INFO", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *serial_lbl = lv_label_create(info_sec);
    lv_label_set_text(serial_lbl, "Serial Number");
    lv_obj_align(serial_lbl, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_text_font(serial_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(serial_lbl, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *serial_val = lv_label_create(info_sec);
    lv_label_set_text(serial_val, "RDM7-SANDBOX-0001");
    lv_obj_align(serial_val, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_text_font(serial_val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(serial_val, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *fw_lbl = lv_label_create(info_sec);
    lv_label_set_text(fw_lbl, "Firmware");
    lv_obj_align(fw_lbl, LV_ALIGN_TOP_LEFT, 0, 62);
    lv_obj_set_style_text_font(fw_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(fw_lbl, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *fw_val = lv_label_create(info_sec);
    lv_label_set_text(fw_val, "v2.4.1 (sandbox)");
    lv_obj_align(fw_val, LV_ALIGN_TOP_LEFT, 0, 76);
    lv_obj_set_style_text_font(fw_val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(fw_val, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *mac_lbl = lv_label_create(info_sec);
    lv_label_set_text(mac_lbl, "MAC");
    lv_obj_align(mac_lbl, LV_ALIGN_TOP_LEFT, 0, 102);
    lv_obj_set_style_text_font(mac_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(mac_lbl, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *mac_val = lv_label_create(info_sec);
    lv_label_set_text(mac_val, "A4:CF:12:AB:CD:EF");
    lv_obj_align(mac_val, LV_ALIGN_TOP_LEFT, 0, 116);
    lv_obj_set_style_text_font(mac_val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(mac_val, THEME_COLOR_TEXT_PRIMARY, 0);

    /* ── Row 2: NETWORK & UPDATES | DISPLAY ──────────────────────── */
    lv_obj_t *row2 = _make_row(content, 260);

    /* NETWORK & UPDATES */
    lv_obj_t *net_sec = _make_section(row2);
    _make_section_title(net_sec, "NETWORK & UPDATES", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *wifi_btn = lv_btn_create(net_sec);
    lv_obj_set_size(wifi_btn, 180, 30);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_bg_color(wifi_btn, THEME_COLOR_BTN_SAVE, 0);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(wifi_btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_shadow_width(wifi_btn, 0, 0);
    lv_obj_t *wifi_blbl = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_blbl, "Wi-Fi Settings");
    lv_obj_center(wifi_blbl);
    lv_obj_set_style_text_font(wifi_blbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(wifi_blbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(wifi_btn, _open_wifi_cb, LV_EVENT_CLICKED, NULL);

    s_wifi_status_label = lv_label_create(net_sec);
    lv_label_set_text(s_wifi_status_label, "WiFi: Home_2.4 (-42 dBm)");
    lv_obj_align(s_wifi_status_label, LV_ALIGN_TOP_LEFT, 0, 60);
    lv_obj_set_style_text_font(s_wifi_status_label, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_wifi_status_label, THEME_COLOR_STATUS_CONNECTED, 0);

    s_web_status_label = lv_label_create(net_sec);
    lv_label_set_text(s_web_status_label, "Web: http://192.168.1.42");
    lv_obj_align(s_web_status_label, LV_ALIGN_TOP_LEFT, 0, 75);
    lv_obj_set_style_text_font(s_web_status_label, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_web_status_label, THEME_COLOR_TEXT_MUTED, 0);

    s_ap_status_label = lv_label_create(net_sec);
    lv_label_set_text(s_ap_status_label, "Hotspot: Disabled");
    lv_obj_align(s_ap_status_label, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_obj_set_style_text_font(s_ap_status_label, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ap_status_label, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *qr_btn = lv_btn_create(net_sec);
    lv_obj_set_size(qr_btn, 110, 30);
    lv_obj_align(qr_btn, LV_ALIGN_TOP_LEFT, 0, 115);
    _style_primary_btn(qr_btn);
    lv_obj_t *qr_lbl = lv_label_create(qr_btn);
    lv_label_set_text(qr_lbl, "Show QR");
    lv_obj_center(qr_lbl);
    lv_obj_set_style_text_font(qr_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(qr_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(qr_btn, _placeholder_cb, LV_EVENT_CLICKED, "QR code - preview");

    lv_obj_t *upd_btn = lv_btn_create(net_sec);
    lv_obj_set_size(upd_btn, 140, 30);
    lv_obj_align(upd_btn, LV_ALIGN_TOP_LEFT, 120, 115);
    _style_secondary_btn(upd_btn);
    lv_obj_t *upd_lbl = lv_label_create(upd_btn);
    lv_label_set_text(upd_lbl, "Check Updates");
    lv_obj_center(upd_lbl);
    lv_obj_set_style_text_font(upd_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(upd_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(upd_btn, _placeholder_cb, LV_EVENT_CLICKED, "OTA check - preview");

    lv_obj_t *net_hint = lv_label_create(net_sec);
    lv_label_set_text(net_hint, "Connect phone or laptop to edit layouts");
    lv_obj_align(net_hint, LV_ALIGN_TOP_LEFT, 0, 155);
    lv_obj_set_style_text_font(net_hint, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(net_hint, THEME_COLOR_TEXT_HINT, 0);

    /* DISPLAY */
    lv_obj_t *disp_sec = _make_section(row2);
    _make_section_title(disp_sec, "DISPLAY", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *bri_lbl = lv_label_create(disp_sec);
    lv_label_set_text(bri_lbl, "Brightness");
    lv_obj_align(bri_lbl, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_text_font(bri_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bri_lbl, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *bri_slider = lv_slider_create(disp_sec);
    lv_obj_set_size(bri_slider, 220, 20);
    lv_obj_align(bri_slider, LV_ALIGN_TOP_LEFT, 0, 45);
    lv_slider_set_range(bri_slider, 5, 100);
    lv_slider_set_value(bri_slider, s_brightness_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bri_slider, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(bri_slider, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bri_slider, THEME_RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(bri_slider, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bri_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bri_slider, THEME_RADIUS_PILL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bri_slider, THEME_COLOR_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(bri_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(bri_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bri_slider, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(bri_slider, _brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_brightness_val_label = lv_label_create(disp_sec);
    lv_label_set_text_fmt(s_brightness_val_label, "%d%%", s_brightness_pct);
    lv_obj_align(s_brightness_val_label, LV_ALIGN_TOP_LEFT, 230, 48);
    lv_obj_set_style_text_font(s_brightness_val_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_brightness_val_label, THEME_COLOR_TEXT_PRIMARY, 0);

    /* Dimmer Switch Config (placeholder) */
    lv_obj_t *dim_btn = lv_btn_create(disp_sec);
    lv_obj_set_size(dim_btn, 250, 30);
    lv_obj_align(dim_btn, LV_ALIGN_TOP_LEFT, 0, 80);
    _style_secondary_btn(dim_btn);
    lv_obj_t *dim_lbl = lv_label_create(dim_btn);
    lv_label_set_text(dim_lbl, "Dimmer Switch Config");
    lv_obj_center(dim_lbl);
    lv_obj_set_style_text_font(dim_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(dim_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(dim_btn, _open_dimmer_cb, LV_EVENT_CLICKED, NULL);

    /* Night mode toggle */
    lv_obj_t *night_btn = lv_btn_create(disp_sec);
    lv_obj_set_size(night_btn, 250, 30);
    lv_obj_align(night_btn, LV_ALIGN_TOP_LEFT, 0, 120);
    _style_secondary_btn(night_btn);
    s_night_btn_label = lv_label_create(night_btn);
    lv_label_set_text(s_night_btn_label, s_night_mode_on ? "Night Mode: ON" : "Night Mode: OFF");
    lv_obj_center(s_night_btn_label);
    lv_obj_set_style_text_font(s_night_btn_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_night_btn_label, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(night_btn, _night_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Rotation toggle — unlike the firmware (where it's hidden pending
     * RGB driver support), the sandbox display *does* support rotation,
     * so we expose it. */
    lv_obj_t *rot_btn = lv_btn_create(disp_sec);
    lv_obj_set_size(rot_btn, 250, 30);
    lv_obj_align(rot_btn, LV_ALIGN_TOP_LEFT, 0, 160);
    _style_secondary_btn(rot_btn);
    s_rotation_btn_label = lv_label_create(rot_btn);
    lv_label_set_text(s_rotation_btn_label, "Rotation: 0\xC2\xB0");
    lv_obj_center(s_rotation_btn_label);
    lv_obj_set_style_text_font(s_rotation_btn_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_rotation_btn_label, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(rot_btn, _rotation_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ── Row 3: DATA LOGGING | PEAK HOLD | TESTING ───────────────── */
    lv_obj_t *row3 = _make_row(content, 95);

    /* DATA LOGGING */
    lv_obj_t *log_sec = _make_section(row3);
    _make_section_title(log_sec, "DATA LOGGING", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *log_btn = lv_btn_create(log_sec);
    lv_obj_set_size(log_btn, 130, 30);
    lv_obj_align(log_btn, LV_ALIGN_TOP_LEFT, 0, 22);
    _style_secondary_btn(log_btn);
    s_log_btn_label = lv_label_create(log_btn);
    lv_label_set_text(s_log_btn_label, "Start Logging");
    lv_obj_center(s_log_btn_label);
    lv_obj_set_style_text_font(s_log_btn_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_log_btn_label, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(log_btn, _log_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *log_rate_dd = lv_dropdown_create(log_sec);
    lv_dropdown_set_options_static(log_rate_dd,
        "1 Hz\n2 Hz\n5 Hz\n10 Hz\n20 Hz\n50 Hz\n100 Hz\n200 Hz\nMax");
    lv_obj_set_size(log_rate_dd, 90, 30);
    lv_obj_align(log_rate_dd, LV_ALIGN_TOP_LEFT, 138, 22);
    _style_dropdown(log_rate_dd);
    lv_dropdown_set_selected(log_rate_dd, 5);  /* 50 Hz */
    lv_obj_add_event_cb(log_rate_dd, _dropdown_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_log_status_label = lv_label_create(log_sec);
    lv_label_set_text(s_log_status_label, "Stopped");
    lv_obj_align(s_log_status_label, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_set_style_text_font(s_log_status_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_log_status_label, THEME_COLOR_TEXT_MUTED, 0);

    /* PEAK HOLD */
    lv_obj_t *peak_sec = _make_section(row3);
    _make_section_title(peak_sec, "PEAK HOLD", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *view_btn = lv_btn_create(peak_sec);
    lv_obj_set_size(view_btn, 110, 30);
    lv_obj_align(view_btn, LV_ALIGN_TOP_LEFT, 0, 22);
    _style_secondary_btn(view_btn);
    lv_obj_t *view_lbl = lv_label_create(view_btn);
    lv_label_set_text(view_lbl, "View Peaks");
    lv_obj_center(view_lbl);
    lv_obj_set_style_text_font(view_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(view_lbl, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_add_event_cb(view_btn, _open_peaks_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_btn = lv_btn_create(peak_sec);
    lv_obj_set_size(reset_btn, 110, 30);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_LEFT, 118, 22);
    _style_secondary_btn(reset_btn);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset Peaks");
    lv_obj_center(reset_lbl);
    lv_obj_set_style_text_font(reset_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(reset_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(reset_btn, _placeholder_cb, LV_EVENT_CLICKED, "Peaks cleared");

    lv_obj_t *peak_note = lv_label_create(peak_sec);
    lv_label_set_text(peak_note, "All-time - persists until reset");
    lv_obj_align(peak_note, LV_ALIGN_TOP_LEFT, 0, 58);
    lv_obj_set_style_text_font(peak_note, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(peak_note, THEME_COLOR_TEXT_MUTED, 0);

    /* TESTING */
    lv_obj_t *test_sec = _make_section(row3);
    _make_section_title(test_sec, "TESTING", THEME_COLOR_TEXT_MUTED);

    lv_obj_t *sim_btn = lv_btn_create(test_sec);
    lv_obj_set_size(sim_btn, 130, 30);
    lv_obj_align(sim_btn, LV_ALIGN_TOP_LEFT, 0, 22);
    _style_secondary_btn(sim_btn);
    s_sim_btn_label = lv_label_create(sim_btn);
    lv_label_set_text(s_sim_btn_label, s_sim_active ? "Sim: ON" : "Sim: OFF");
    lv_obj_center(s_sim_btn_label);
    lv_obj_set_style_text_font(s_sim_btn_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_sim_btn_label,
        s_sim_active ? THEME_COLOR_STATUS_CONNECTED : THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(sim_btn, _sim_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *test_note = lv_label_create(test_sec);
    lv_label_set_text(test_note, "Sweep all signals - demo only");
    lv_obj_align(test_note, LV_ALIGN_TOP_LEFT, 0, 58);
    lv_obj_set_style_text_font(test_note, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(test_note, THEME_COLOR_TEXT_MUTED, 0);

    /* ── CAN BUS Diagnostics (full-width) ────────────────────────── */
    lv_obj_t *diag_sec = lv_obj_create(content);
    lv_obj_set_size(diag_sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(diag_sec, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(diag_sec, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(diag_sec, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_side(diag_sec, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(diag_sec, THEME_COLOR_SECTION_CAN_TITLE, 0);
    lv_obj_set_style_border_width(diag_sec, 3, 0);
    lv_obj_set_style_pad_all(diag_sec, 10, 0);
    lv_obj_set_style_pad_left(diag_sec, 12, 0);
    lv_obj_set_style_pad_row(diag_sec, 5, 0);
    lv_obj_clear_flag(diag_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(diag_sec, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title_row = lv_obj_create(diag_sec);
    lv_obj_set_size(title_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, 0, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *diag_title = lv_label_create(title_row);
    lv_label_set_text(diag_title, "CAN BUS");
    lv_obj_align(diag_title, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(diag_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(diag_title, THEME_COLOR_SECTION_CAN_TITLE, 0);
    lv_obj_set_style_text_letter_space(diag_title, 1, 0);

    lv_obj_t *scan_btn = lv_btn_create(title_row);
    lv_obj_set_size(scan_btn, 110, 24);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    _style_primary_btn(scan_btn);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Run Bus Scan");
    lv_obj_center(scan_lbl);
    lv_obj_set_style_text_font(scan_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(scan_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(scan_btn, _open_bus_scan_cb, LV_EVENT_CLICKED, NULL);

    /* Health row */
    lv_obj_t *health_row = lv_obj_create(diag_sec);
    lv_obj_set_size(health_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(health_row, 0, 0);
    lv_obj_set_style_border_width(health_row, 0, 0);
    lv_obj_set_style_pad_all(health_row, 0, 0);
    lv_obj_set_style_pad_column(health_row, 6, 0);
    lv_obj_clear_flag(health_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(health_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(health_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_can_health_dot = lv_obj_create(health_row);
    lv_obj_set_size(s_can_health_dot, 8, 8);
    lv_obj_set_style_radius(s_can_health_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_can_health_dot, THEME_COLOR_STATUS_CONNECTED, 0);
    lv_obj_set_style_bg_opa(s_can_health_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_can_health_dot, 0, 0);
    lv_obj_clear_flag(s_can_health_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_can_health_label = lv_label_create(health_row);
    lv_label_set_text(s_can_health_label, "OK - receiving 142 fps");
    lv_obj_set_style_text_font(s_can_health_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_can_health_label, THEME_COLOR_TEXT_MUTED, 0);

    s_can_summary_label = lv_label_create(diag_sec);
    lv_label_set_text(s_can_summary_label, "500 kbps - last ID 0x360 - 142 fps");
    lv_obj_set_style_text_font(s_can_summary_label, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_can_summary_label, THEME_COLOR_TEXT_MUTED, 0);

    s_can_details_toggle = lv_label_create(diag_sec);
    lv_label_set_text(s_can_details_toggle, LV_SYMBOL_RIGHT " Show Details");
    lv_obj_set_style_text_font(s_can_details_toggle, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_can_details_toggle, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_add_flag(s_can_details_toggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_can_details_toggle, _details_toggle_cb, LV_EVENT_CLICKED, NULL);

    s_can_details_grid = lv_obj_create(diag_sec);
    lv_obj_set_size(s_can_details_grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_can_details_grid, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(s_can_details_grid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_can_details_grid, 0, 0);
    lv_obj_set_style_radius(s_can_details_grid, THEME_RADIUS_SMALL, 0);
    lv_obj_set_style_pad_all(s_can_details_grid, 6, 0);
    lv_obj_set_style_pad_column(s_can_details_grid, 8, 0);
    lv_obj_set_style_pad_row(s_can_details_grid, 2, 0);
    lv_obj_clear_flag(s_can_details_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_can_details_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(s_can_details_grid, LV_OBJ_FLAG_HIDDEN);
    s_details_open = false;

    static const char *detail_defaults[6] = {
        "RX Count: 0", "RX Errors: 0", "RX Missed: 0",
        "TX Count: 0", "TX Errors: 0", "Bus Errors: 0",
    };
    for (int i = 0; i < 6; i++) {
        s_can_detail_labels[i] = lv_label_create(s_can_details_grid);
        lv_label_set_text(s_can_detail_labels[i], detail_defaults[i]);
        lv_obj_set_width(s_can_detail_labels[i], 200);
        lv_obj_set_style_text_font(s_can_detail_labels[i], THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_can_detail_labels[i], THEME_COLOR_TEXT_MUTED, 0);
    }

    /* ── Footer buttons ──────────────────────────────────────────── */
    _footer_btn(content, "System Diagnostics", THEME_COLOR_TEXT_PRIMARY,
                _open_diag_cb, NULL);
    _footer_btn(content, "Run Setup Wizard", THEME_COLOR_TEXT_PRIMARY,
                _run_wizard_cb, NULL);
    _footer_btn(content, "Reset to Default", THEME_COLOR_STATUS_ERROR,
                _placeholder_cb, "Factory Reset - preview");

    /* Restart the diagnostics refresh timer. */
    if (s_can_diag_timer) { lv_timer_del(s_can_diag_timer); s_can_diag_timer = NULL; }
    s_can_diag_timer = lv_timer_create(_can_diag_tick, 1000, NULL);

    lv_scr_load(s_settings_screen);
}

/* Public teardown used by the scene dispatcher when rewinding. */
void device_settings_sandbox_close(void) {
    if (s_can_diag_timer) { lv_timer_del(s_can_diag_timer); s_can_diag_timer = NULL; }
    lv_obj_t *scr = s_settings_screen;
    s_settings_screen = NULL;
    s_content_col = NULL;
    if (scr && lv_obj_is_valid(scr)) lv_obj_del(scr);
    s_brightness_val_label = s_rotation_btn_label = s_night_btn_label = NULL;
    s_sim_btn_label = s_log_btn_label = s_log_status_label = NULL;
    s_wifi_status_label = s_web_status_label = s_ap_status_label = NULL;
    s_can_health_dot = s_can_health_label = s_can_summary_label = NULL;
    s_can_details_toggle = s_can_details_grid = NULL;
    for (int i = 0; i < 6; i++) s_can_detail_labels[i] = NULL;
}

/* ── JS bridge: scroll the content column ────────────────────────────
 * Tours call this with a section id (see enum below) to jump the
 * scrollable content view to a known position before the caption
 * explains that section. Id range kept open-ended so we can add
 * fine-grained stops without breaking older tour scripts. */
enum {
    DS_SECTION_TOP          = 0,  /* CAN BUS + DEVICE INFO row          */
    DS_SECTION_NETWORK      = 1,  /* NETWORK & UPDATES / DISPLAY row    */
    DS_SECTION_LOGGING      = 2,  /* DATA LOGGING / PEAK HOLD / TESTING */
    DS_SECTION_CAN_DIAG     = 3,  /* CAN BUS diagnostics                */
    DS_SECTION_FOOTER       = 4,  /* System Diag / Setup Wizard / Reset */
    DS_SECTION_DISPLAY      = 5,  /* Same row as NETWORK — right column */
    DS_SECTION_PEAKS        = 6,  /* Same row as LOGGING — middle col   */
};

/* Approximate y offsets within the flex-column. Values tuned against
 * the section heights set in device_settings_sandbox_open — keep in
 * sync if the row heights change. */
static const int s_section_y[] = {
    [DS_SECTION_TOP]       = 0,
    [DS_SECTION_NETWORK]   = 172,
    [DS_SECTION_DISPLAY]   = 172,
    [DS_SECTION_LOGGING]   = 444,
    [DS_SECTION_PEAKS]     = 444,
    [DS_SECTION_CAN_DIAG]  = 555,
    [DS_SECTION_FOOTER]    = 760,
};

EMSCRIPTEN_KEEPALIVE
void sandbox_device_settings_scroll(int section_id) {
    if (!s_content_col || !lv_obj_is_valid(s_content_col)) return;
    if (section_id < 0 || section_id >= (int)(sizeof(s_section_y)/sizeof(s_section_y[0]))) return;
    int y = s_section_y[section_id];
    lv_obj_scroll_to_y(s_content_col, y, LV_ANIM_ON);
}
