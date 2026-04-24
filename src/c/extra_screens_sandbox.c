/* extra_screens_sandbox.c — three secondary screens in one module.
 *
 *   wifi_settings_sandbox_open()      — ui_wifi.c equivalent
 *   peaks_sandbox_open()              — ui_peaks.c equivalent
 *   diagnostics_sandbox_open()        — ui_diagnostics.c equivalent
 *
 * Each replaces the toast-stub it used to trigger from Device Settings.
 * The real firmware versions are 400–800 lines each; the sandbox clones
 * keep the visual shell faithful and wire just enough behaviour that
 * the tutorial can narrate them without the pane feeling dead.
 *
 * All three follow the same pattern as device_settings_sandbox.c — a
 * full-screen backdrop with a centred card, header + body, Back button
 * that returns to the stored return_screen (usually device settings). */

#include "lvgl.h"
#include "esp_idf_shim.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Theme ───────────────────────────────────────────────────────────── */
#define THEME_COLOR_BG                  lv_color_hex(0x000000)
#define THEME_COLOR_SURFACE             lv_color_hex(0x292C29)
#define THEME_COLOR_SECTION_BG          lv_color_hex(0x393C39)
#define THEME_COLOR_INPUT_BG            lv_color_hex(0x181C18)
#define THEME_COLOR_BORDER              lv_color_hex(0x181C18)
#define THEME_COLOR_TEXT_PRIMARY        lv_color_hex(0xE8E8E8)
#define THEME_COLOR_TEXT_MUTED          lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_HINT           lv_color_hex(0x606260)
#define THEME_COLOR_TEXT_ON_ACCENT      lv_color_hex(0xFFFFFF)
#define THEME_COLOR_ACCENT_BLUE         lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_BLUE_PRESSED lv_color_hex(0x1976D2)
#define THEME_COLOR_ACCENT_TEAL         lv_color_hex(0x26C6DA)
#define THEME_COLOR_ACCENT_GREEN        lv_color_hex(0x4CAF50)
#define THEME_COLOR_ACCENT_AMBER        lv_color_hex(0xFFC107)
#define THEME_COLOR_STATUS_ERROR        lv_color_hex(0xF84040)
#define THEME_FONT_TINY                 (&lv_font_montserrat_10)
#define THEME_FONT_SMALL                (&lv_font_montserrat_12)
#define THEME_FONT_MEDIUM               (&lv_font_montserrat_14)
#define THEME_RADIUS_SMALL              2
#define THEME_RADIUS_NORMAL             4

/* ── Shared screen state ─────────────────────────────────────────────── */
static lv_obj_t *s_wifi_screen = NULL;
static lv_obj_t *s_peaks_screen = NULL;
static lv_obj_t *s_diag_screen = NULL;
static lv_obj_t *s_return_screen = NULL;

/* Widgets we update live */
static lv_obj_t *s_wifi_status_label = NULL;
static lv_obj_t *s_peaks_rows = NULL;
static lv_timer_t *s_diag_refresh_timer = NULL;

/* ─────────────────────────────────────────────────────────────────────
 *  Common helpers
 * ───────────────────────────────────────────────────────────────────── */

static void _style_secondary_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void _style_accent_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_color(btn, THEME_COLOR_ACCENT_BLUE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
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

/* Build a header bar with title + Back button. Returns the header obj
 * so callers can append extra controls (Reset, Refresh, etc.). */
static lv_obj_t *_build_header(lv_obj_t *parent, const char *title,
                               lv_event_cb_t back_cb) {
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, 70, 26);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    _style_secondary_btn(back);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_center(back_lbl);
    lv_obj_set_style_text_font(back_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(back_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tlbl = lv_label_create(header);
    lv_label_set_text(tlbl, title);
    lv_obj_align(tlbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(tlbl, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(tlbl, THEME_COLOR_TEXT_PRIMARY, 0);

    return header;
}

static void _return_to_prev(lv_obj_t **owned_screen) {
    lv_obj_t *scr = *owned_screen;
    lv_obj_t *ret = s_return_screen;
    *owned_screen = NULL;
    if (ret && lv_obj_is_valid(ret)) lv_scr_load(ret);
    if (scr && lv_obj_is_valid(scr)) lv_obj_del_async(scr);
}

/* ─────────────────────────────────────────────────────────────────────
 *  Wi-Fi Settings screen
 * ───────────────────────────────────────────────────────────────────── */

static const struct { const char *ssid; int8_t rssi; bool secured; } s_wifi_networks[] = {
    { "Home_2.4",     -42, true  },
    { "CarWiFi",      -55, true  },
    { "iPhone_Tommy", -58, true  },
    { "TeslaGuest",   -71, false },
    { "Neighbor5G",   -78, true  },
    { "ShopNetwork",  -82, true  },
};
#define S_WIFI_NET_COUNT (sizeof(s_wifi_networks)/sizeof(s_wifi_networks[0]))

static const char *_rssi_bars(int8_t rssi) {
    if (rssi >= -50) return LV_SYMBOL_WIFI " strong";
    if (rssi >= -65) return LV_SYMBOL_WIFI " good";
    if (rssi >= -75) return LV_SYMBOL_WIFI " ok";
    return LV_SYMBOL_WIFI " weak";
}

static void _wifi_back_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_wifi_status_label = NULL;
    _return_to_prev(&s_wifi_screen);
}

static void _wifi_network_row_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (s_wifi_status_label && lv_obj_is_valid(s_wifi_status_label))
        lv_label_set_text_fmt(s_wifi_status_label, "Connected (%s)", ssid ? ssid : "?");
}

static void _wifi_noop_cb(lv_event_t *e) { (void)e; }

void wifi_settings_sandbox_open(lv_obj_t *return_screen) {
    s_return_screen = return_screen ? return_screen : lv_scr_act();

    s_wifi_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_wifi_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_wifi_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_wifi_screen, LV_OPA_COVER, 0);

    lv_obj_t *main = lv_obj_create(s_wifi_screen);
    lv_obj_set_size(main, 780, 460);
    lv_obj_center(main);
    lv_obj_set_style_bg_color(main, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(main, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(main, 1, 0);
    lv_obj_set_style_radius(main, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = _build_header(main, LV_SYMBOL_WIFI "  Wi-Fi Settings", _wifi_back_cb);
    s_wifi_status_label = lv_label_create(header);
    lv_label_set_text(s_wifi_status_label, "Connected (Home_2.4)");
    lv_obj_align(s_wifi_status_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_font(s_wifi_status_label, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_wifi_status_label, THEME_COLOR_ACCENT_GREEN, 0);

    /* Body: two side-by-side cards */
    lv_obj_t *body = lv_obj_create(main);
    lv_obj_set_size(body, lv_pct(100), 410);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_opa(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_gap(body, 12, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    /* ── Left card: mode + boot + hotspot password ─────────────── */
    lv_obj_t *left = lv_obj_create(body);
    lv_obj_set_size(left, 340, lv_pct(100));
    lv_obj_set_style_bg_color(left, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(left, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_pad_all(left, 14, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left_title = lv_label_create(left);
    lv_label_set_text(left_title, "CONTROLS");
    lv_obj_align(left_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(left_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(left_title, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_letter_space(left_title, 1, 0);

    lv_obj_t *mode_cap = lv_label_create(left);
    lv_label_set_text(mode_cap, "Mode");
    lv_obj_align(mode_cap, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_set_style_text_font(mode_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(mode_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *mode_dd = lv_dropdown_create(left);
    lv_dropdown_set_options(mode_dd, "Off\nWiFi (Client)\nHotspot (AP)");
    lv_obj_set_size(mode_dd, lv_pct(100), 32);
    lv_obj_align(mode_dd, LV_ALIGN_TOP_LEFT, 0, 38);
    _style_dropdown(mode_dd);
    lv_dropdown_set_selected(mode_dd, 1);
    lv_obj_add_event_cb(mode_dd, _wifi_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *boot_cap = lv_label_create(left);
    lv_label_set_text(boot_cap, "Start on Boot");
    lv_obj_align(boot_cap, LV_ALIGN_TOP_LEFT, 0, 82);
    lv_obj_set_style_text_font(boot_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(boot_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *boot_dd = lv_dropdown_create(left);
    lv_dropdown_set_options(boot_dd, "Off\nWiFi (Client)\nHotspot (AP)");
    lv_obj_set_size(boot_dd, lv_pct(100), 32);
    lv_obj_align(boot_dd, LV_ALIGN_TOP_LEFT, 0, 98);
    _style_dropdown(boot_dd);
    lv_dropdown_set_selected(boot_dd, 1);
    lv_obj_add_event_cb(boot_dd, _wifi_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *pwd_cap = lv_label_create(left);
    lv_label_set_text(pwd_cap, "Hotspot Password");
    lv_obj_align(pwd_cap, LV_ALIGN_TOP_LEFT, 0, 144);
    lv_obj_set_style_text_font(pwd_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(pwd_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *pwd_ta = lv_textarea_create(left);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Min 8 chars");
    lv_obj_set_size(pwd_ta, 220, 32);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_LEFT, 0, 160);
    lv_obj_set_style_bg_color(pwd_ta, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_text_color(pwd_ta, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(pwd_ta, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(pwd_ta, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(pwd_ta, 1, 0);
    lv_obj_set_style_radius(pwd_ta, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(pwd_ta, 6, 0);

    lv_obj_t *set_btn = lv_btn_create(left);
    lv_obj_set_size(set_btn, 80, 32);
    lv_obj_align(set_btn, LV_ALIGN_TOP_LEFT, 226, 160);
    _style_accent_btn(set_btn);
    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, "Set");
    lv_obj_center(set_lbl);
    lv_obj_set_style_text_font(set_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(set_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(set_btn, _wifi_noop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ap_info = lv_label_create(left);
    lv_label_set_text(ap_info,
        "AP SSID: RDM7-Dash\nAP IP:   192.168.4.1\nClients: 0 connected");
    lv_obj_align(ap_info, LV_ALIGN_TOP_LEFT, 0, 210);
    lv_obj_set_style_text_font(ap_info, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(ap_info, THEME_COLOR_TEXT_MUTED, 0);

    /* ── Right card: network list ───────────────────────────────── */
    lv_obj_t *right = lv_obj_create(body);
    lv_obj_set_size(right, 380, lv_pct(100));
    lv_obj_set_style_bg_color(right, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(right, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_pad_all(right, 14, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *right_title = lv_label_create(right);
    lv_label_set_text(right_title, "AVAILABLE NETWORKS");
    lv_obj_align(right_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(right_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(right_title, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_letter_space(right_title, 1, 0);

    lv_obj_t *scan_btn = lv_btn_create(right);
    lv_obj_set_size(scan_btn, 120, 26);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, 0, -4);
    _style_accent_btn(scan_btn);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan Networks");
    lv_obj_center(scan_lbl);
    lv_obj_set_style_text_font(scan_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(scan_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(scan_btn, _wifi_noop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_obj_create(right);
    lv_obj_set_size(list, lv_pct(100), 300);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_bg_color(list, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, THEME_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_color(list, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    for (size_t i = 0; i < S_WIFI_NET_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, lv_pct(100), 40);
        lv_obj_set_style_bg_color(row, THEME_COLOR_SECTION_BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, THEME_RADIUS_SMALL, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, _wifi_network_row_cb, LV_EVENT_CLICKED,
                            (void *)s_wifi_networks[i].ssid);

        lv_obj_t *ssid = lv_label_create(row);
        lv_label_set_text_fmt(ssid, "%s %s",
            s_wifi_networks[i].secured ? LV_SYMBOL_EYE_CLOSE : "  ",
            s_wifi_networks[i].ssid);
        lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_font(ssid, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(ssid, THEME_COLOR_TEXT_PRIMARY, 0);

        lv_obj_t *rssi = lv_label_create(row);
        lv_label_set_text(rssi, _rssi_bars(s_wifi_networks[i].rssi));
        lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_font(rssi, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(rssi, THEME_COLOR_TEXT_MUTED, 0);
    }

    lv_obj_t *note = lv_label_create(right);
    lv_label_set_text(note, "Tap a network to connect.");
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(note, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(note, THEME_COLOR_TEXT_HINT, 0);

    lv_scr_load(s_wifi_screen);
}

void wifi_settings_sandbox_close(void) {
    if (s_wifi_screen && lv_obj_is_valid(s_wifi_screen)) {
        lv_obj_t *scr = s_wifi_screen;
        s_wifi_screen = NULL;
        s_wifi_status_label = NULL;
        lv_obj_del(scr);
    }
}

EMSCRIPTEN_KEEPALIVE
void sandbox_open_wifi_settings(void) {
    wifi_settings_sandbox_open(lv_scr_act());
}

/* ─────────────────────────────────────────────────────────────────────
 *  Peaks table
 * ───────────────────────────────────────────────────────────────────── */

typedef struct { const char *name; const char *cur; const char *min; const char *max; } peak_row_t;
static peak_row_t s_peaks[] = {
    { "RPM",             "3200", "800",   "7240" },
    { "MAP",             "101",  "24",    "187"  },
    { "THROTTLE",        "34",   "0",     "100"  },
    { "COOLANT_TEMP",    "88",   "22",    "104"  },
    { "INTAKE_AIR_TEMP", "42",   "18",    "62"   },
    { "LAMBDA",          "0.98", "0.68",  "1.22" },
    { "OIL_TEMP",        "95",   "24",    "118"  },
    { "OIL_PRESSURE",    "64",   "22",    "98"   },
    { "FUEL_PRESSURE",   "49",   "42",    "55"   },
    { "VEHICLE_SPEED",   "82",   "0",     "231"  },
    { "GEAR",            "4",    "0",     "6"    },
    { "BATTERY_VOLTAGE", "14.2", "12.6",  "14.8" },
    { "FUEL_TRIM",       "2.1",  "-4.8",  "6.2"  },
};
#define S_PEAKS_COUNT (sizeof(s_peaks)/sizeof(s_peaks[0]))

static void _peaks_back_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_peaks_rows = NULL;
    _return_to_prev(&s_peaks_screen);
}

static void _peaks_reset_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!s_peaks_rows || !lv_obj_is_valid(s_peaks_rows)) return;
    /* Walk child rows and zero out the min/max columns to show a reset. */
    uint32_t count = lv_obj_get_child_cnt(s_peaks_rows);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_get_child(s_peaks_rows, i);
        uint32_t cols = lv_obj_get_child_cnt(row);
        for (uint32_t j = 0; j < cols; j++) {
            lv_obj_t *lbl = lv_obj_get_child(row, j);
            if (!lbl) continue;
            /* Column index: 0 = name, 1 = current, 2 = min, 3 = max */
            if (j == 2 || j == 3) lv_label_set_text(lbl, "---");
        }
    }
}

void peaks_sandbox_open(lv_obj_t *return_screen) {
    s_return_screen = return_screen ? return_screen : lv_scr_act();

    s_peaks_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_peaks_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_peaks_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_peaks_screen, LV_OPA_COVER, 0);

    lv_obj_t *header = _build_header(s_peaks_screen, "Signal Peaks", _peaks_back_cb);
    lv_obj_t *reset_btn = lv_btn_create(header);
    lv_obj_set_size(reset_btn, 100, 26);
    lv_obj_align(reset_btn, LV_ALIGN_RIGHT_MID, -6, 0);
    _style_secondary_btn(reset_btn);
    lv_obj_set_style_border_color(reset_btn, THEME_COLOR_STATUS_ERROR, 0);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset All");
    lv_obj_center(reset_lbl);
    lv_obj_set_style_text_font(reset_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(reset_lbl, THEME_COLOR_STATUS_ERROR, 0);
    lv_obj_add_event_cb(reset_btn, _peaks_reset_cb, LV_EVENT_CLICKED, NULL);

    /* Column header */
    lv_obj_t *col = lv_obj_create(s_peaks_screen);
    lv_obj_set_size(col, lv_pct(100), 28);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(col, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    const char *col_names[] = { "Signal", "Current", "Min", "Max" };
    const int col_widths[]  = { 40, 20, 20, 20 };  /* percent */
    int cursor_x = 0;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *h = lv_label_create(col);
        lv_label_set_text(h, col_names[i]);
        lv_obj_set_style_text_font(h, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(h, THEME_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_letter_space(h, 1, 0);
        if (i == 0) lv_obj_align(h, LV_ALIGN_LEFT_MID,  14 + cursor_x, 0);
        else        lv_obj_align(h, LV_ALIGN_LEFT_MID,  14 + cursor_x, 0);
        cursor_x += (800 * col_widths[i]) / 100;
    }

    /* Scrollable rows */
    s_peaks_rows = lv_obj_create(s_peaks_screen);
    lv_obj_set_size(s_peaks_rows, lv_pct(100), 408);
    lv_obj_align(s_peaks_rows, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_bg_color(s_peaks_rows, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_peaks_rows, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_peaks_rows, 0, 0);
    lv_obj_set_style_border_width(s_peaks_rows, 0, 0);
    lv_obj_set_style_pad_all(s_peaks_rows, 0, 0);
    lv_obj_set_style_pad_row(s_peaks_rows, 1, 0);
    lv_obj_set_scroll_dir(s_peaks_rows, LV_DIR_VER);
    lv_obj_set_flex_flow(s_peaks_rows, LV_FLEX_FLOW_COLUMN);

    for (size_t i = 0; i < S_PEAKS_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(s_peaks_rows);
        lv_obj_set_size(row, lv_pct(100), 30);
        lv_obj_set_style_bg_color(row, (i & 1) ? THEME_COLOR_SECTION_BG : THEME_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Children ordered: name, current, min, max (matches _peaks_reset_cb) */
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, s_peaks[i].name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_set_style_text_font(name, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(name, THEME_COLOR_TEXT_PRIMARY, 0);

        lv_obj_t *cur = lv_label_create(row);
        lv_label_set_text(cur, s_peaks[i].cur);
        lv_obj_align(cur, LV_ALIGN_LEFT_MID, 14 + 320, 0);
        lv_obj_set_style_text_font(cur, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(cur, THEME_COLOR_ACCENT_TEAL, 0);

        lv_obj_t *mn = lv_label_create(row);
        lv_label_set_text(mn, s_peaks[i].min);
        lv_obj_align(mn, LV_ALIGN_LEFT_MID, 14 + 480, 0);
        lv_obj_set_style_text_font(mn, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(mn, THEME_COLOR_ACCENT_GREEN, 0);

        lv_obj_t *mx = lv_label_create(row);
        lv_label_set_text(mx, s_peaks[i].max);
        lv_obj_align(mx, LV_ALIGN_LEFT_MID, 14 + 640, 0);
        lv_obj_set_style_text_font(mx, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(mx, THEME_COLOR_ACCENT_AMBER, 0);
    }

    lv_scr_load(s_peaks_screen);
}

void peaks_sandbox_close(void) {
    if (s_peaks_screen && lv_obj_is_valid(s_peaks_screen)) {
        lv_obj_t *scr = s_peaks_screen;
        s_peaks_screen = NULL;
        s_peaks_rows = NULL;
        lv_obj_del(scr);
    }
}

EMSCRIPTEN_KEEPALIVE
void sandbox_open_peaks(void) {
    peaks_sandbox_open(lv_scr_act());
}

/* ─────────────────────────────────────────────────────────────────────
 *  System Diagnostics
 * ───────────────────────────────────────────────────────────────────── */

static lv_obj_t *s_diag_uptime_lbl = NULL;

/* Cards hold N "key: value" rows as sibling labels. Keeping the card
 * builder isolated makes the layout predictable; callers supply fixed
 * row strings and we just render them. */
static lv_obj_t *_diag_card(lv_obj_t *parent, const char *title,
                            lv_color_t title_color,
                            const char **rows, int row_count) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 0, 170);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_color(c, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(c, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(t, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(t, title_color, 0);
    lv_obj_set_style_text_letter_space(t, 1, 0);

    for (int i = 0; i < row_count; i++) {
        lv_obj_t *r = lv_label_create(c);
        lv_label_set_text(r, rows[i]);
        lv_obj_align(r, LV_ALIGN_TOP_LEFT, 0, 20 + i * 20);
        lv_obj_set_style_text_font(r, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(r, THEME_COLOR_TEXT_PRIMARY, 0);
    }

    return c;
}

static void _diag_back_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_diag_refresh_timer) { lv_timer_del(s_diag_refresh_timer); s_diag_refresh_timer = NULL; }
    s_diag_uptime_lbl = NULL;
    _return_to_prev(&s_diag_screen);
}

static void _diag_refresh_cb(lv_event_t *e) { (void)e; /* visual-only */ }

static void _diag_tick(lv_timer_t *t) {
    (void)t;
    static uint32_t seconds = 137;  /* fake boot-time counter */
    seconds++;
    if (!s_diag_uptime_lbl || !lv_obj_is_valid(s_diag_uptime_lbl)) return;
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    lv_label_set_text_fmt(s_diag_uptime_lbl, "Uptime: %uh %02um %02us",
                          (unsigned)h, (unsigned)m, (unsigned)s);
}

void diagnostics_sandbox_open(lv_obj_t *return_screen) {
    s_return_screen = return_screen ? return_screen : lv_scr_act();

    s_diag_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_diag_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_diag_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_diag_screen, LV_OPA_COVER, 0);

    lv_obj_t *header = _build_header(s_diag_screen, "System Diagnostics", _diag_back_cb);
    lv_obj_t *refresh_btn = lv_btn_create(header);
    lv_obj_set_size(refresh_btn, 90, 26);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, -6, 0);
    _style_secondary_btn(refresh_btn);
    lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, LV_SYMBOL_REFRESH "  Now");
    lv_obj_center(refresh_lbl);
    lv_obj_set_style_text_font(refresh_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(refresh_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(refresh_btn, _diag_refresh_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *grid = lv_obj_create(s_diag_screen);
    lv_obj_set_size(grid, lv_pct(100), 436);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_opa(grid, 0, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static const char *can_rows[] = {
        "State: RUNNING", "Pending RX: 0", "TX errors: 0",
        "RX errors: 0",   "Bus errors: 0", "RX missed: 0",
    };
    _diag_card(grid, "CAN BUS", THEME_COLOR_ACCENT_BLUE, can_rows, 6);

    static const char *wifi_rows[] = {
        "WiFi:   Connected",   "SSID:   Home_2.4",
        "STA IP: 192.168.1.42", "AP:     Disabled",
        "AP IP:  192.168.4.1",
    };
    _diag_card(grid, "WI-FI", THEME_COLOR_ACCENT_BLUE, wifi_rows, 5);

    /* SYSTEM card — uptime gets a live timer (signals the screen's not frozen). */
    lv_obj_t *sys_card = lv_obj_create(grid);
    lv_obj_set_size(sys_card, 0, 170);
    lv_obj_set_flex_grow(sys_card, 1);
    lv_obj_set_style_bg_color(sys_card, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(sys_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sys_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(sys_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(sys_card, 1, 0);
    lv_obj_set_style_pad_all(sys_card, 12, 0);
    lv_obj_clear_flag(sys_card, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *t = lv_label_create(sys_card);
        lv_label_set_text(t, "SYSTEM");
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_text_font(t, THEME_FONT_TINY, 0);
        lv_obj_set_style_text_color(t, THEME_COLOR_ACCENT_BLUE, 0);
        lv_obj_set_style_text_letter_space(t, 1, 0);

        s_diag_uptime_lbl = lv_label_create(sys_card);
        lv_label_set_text(s_diag_uptime_lbl, "Uptime: 0h 02m 17s");
        lv_obj_align(s_diag_uptime_lbl, LV_ALIGN_TOP_LEFT, 0, 20);
        lv_obj_set_style_text_font(s_diag_uptime_lbl, THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_diag_uptime_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

        static const char *extra[] = {
            "Free heap:  412 KB",
            "Free PSRAM: 6.3 MB",
            "Logger:     Stopped",
            "Replay:     Idle",
        };
        for (int i = 0; i < 4; i++) {
            lv_obj_t *r = lv_label_create(sys_card);
            lv_label_set_text(r, extra[i]);
            lv_obj_align(r, LV_ALIGN_TOP_LEFT, 0, 40 + i * 20);
            lv_obj_set_style_text_font(r, THEME_FONT_SMALL, 0);
            lv_obj_set_style_text_color(r, THEME_COLOR_TEXT_PRIMARY, 0);
        }
    }

    static const char *sd_rows[] = {
        "SD:    Mounted", "Usage: 2.1 GB used", "Free:  13.6 GB",
    };
    _diag_card(grid, "SD CARD", THEME_COLOR_ACCENT_AMBER, sd_rows, 3);

    static const char *sig_rows[] = {
        "Total:  14 registered", "Fresh:  14 (<2 s)", "Stale:  0",
    };
    _diag_card(grid, "SIGNALS", THEME_COLOR_ACCENT_AMBER, sig_rows, 3);

    if (s_diag_refresh_timer) lv_timer_del(s_diag_refresh_timer);
    s_diag_refresh_timer = lv_timer_create(_diag_tick, 1000, NULL);

    lv_scr_load(s_diag_screen);
}

void diagnostics_sandbox_close(void) {
    if (s_diag_refresh_timer) { lv_timer_del(s_diag_refresh_timer); s_diag_refresh_timer = NULL; }
    if (s_diag_screen && lv_obj_is_valid(s_diag_screen)) {
        lv_obj_t *scr = s_diag_screen;
        s_diag_screen = NULL;
        s_diag_uptime_lbl = NULL;
        lv_obj_del(scr);
    }
}

EMSCRIPTEN_KEEPALIVE
void sandbox_open_diagnostics(void) {
    diagnostics_sandbox_open(lv_scr_act());
}

/* ─────────────────────────────────────────────────────────────────────
 *  Dimmer Switch Config popup
 *
 *  Popup (not full-screen). 480x360 card on a semi-transparent backdrop
 *  over whatever is the current screen. Matches the firmware's
 *  brightness_dimmer_config_cb modal in structure & labels.
 *
 *  Fields (all UI-only in sandbox; no NVS persistence):
 *    - Signal Source    dropdown (internal signals + layout signals)
 *    - Threshold        one-line textarea (float, defaults to "0.5")
 *    - Toggle Mode      dropdown  ("On/Off" | "Momentary")
 *    - Invert           switch
 *    - Enabled          switch
 *    - Dim Brightness   slider 5-100%
 *    - Save / Close buttons
 * ───────────────────────────────────────────────────────────────────── */

static lv_obj_t *s_dimmer_overlay = NULL;

static void _dimmer_close(void) {
    lv_obj_t *o = s_dimmer_overlay;
    s_dimmer_overlay = NULL;
    if (o && lv_obj_is_valid(o)) lv_obj_del(o);
}

static void _dimmer_close_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _dimmer_close();
}

static void _dimmer_save_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* Sandbox never persists dimmer config — Save is a UI-only close. */
    _dimmer_close();
}

static void _dimmer_noop_cb(lv_event_t *e) { (void)e; }

/* Keep the %-label next to the Dim Brightness slider in sync as the
 * user drags. user_data = target label. */
static void _dimmer_slider_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *sl = lv_event_get_target(e);
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (!lbl || !lv_obj_is_valid(lbl)) return;
    int v = lv_slider_get_value(sl);
    lv_label_set_text_fmt(lbl, "%d%%", v);
}

void dimmer_config_sandbox_open(void) {
    if (s_dimmer_overlay && lv_obj_is_valid(s_dimmer_overlay)) return;

    /* Full-screen backdrop, semi-transparent, intercepts taps. Tapping
     * the backdrop closes the popup (matches firmware behaviour). */
    s_dimmer_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_dimmer_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(s_dimmer_overlay);
    lv_obj_set_style_bg_color(s_dimmer_overlay, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_dimmer_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_dimmer_overlay, 0, 0);
    lv_obj_clear_flag(s_dimmer_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dimmer_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_dimmer_overlay, _dimmer_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *popup = lv_obj_create(s_dimmer_overlay);
    lv_obj_set_size(popup, 480, 360);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(popup, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(popup, 1, 0);
    lv_obj_set_style_radius(popup, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(popup, 20, 0);
    lv_obj_set_style_shadow_ofs_y(popup, 4, 0);
    lv_obj_set_style_shadow_color(popup, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(popup, 0, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    /* Block backdrop close when the popup itself is tapped. */
    lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(popup);
    lv_obj_set_size(hdr, lv_pct(100), 44);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Brightness Dimmer Switch");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *close_btn = lv_btn_create(hdr);
    lv_obj_set_size(close_btn, 32, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    _style_secondary_btn(close_btn);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(close_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(close_btn, _dimmer_close_cb, LV_EVENT_CLICKED, NULL);

    /* Body */
    lv_obj_t *body = lv_obj_create(popup);
    lv_obj_set_size(body, lv_pct(100), 258);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_opa(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 18, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Row 1 — Signal Source (left) + Threshold (right) */
    lv_obj_t *sig_cap = lv_label_create(body);
    lv_label_set_text(sig_cap, "Signal Source");
    lv_obj_align(sig_cap, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(sig_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sig_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *sig_dd = lv_dropdown_create(body);
    /* Internal signals first (mirrors firmware's _build_signal_options
     * order), then a few layout signals. */
    lv_dropdown_set_options(sig_dd,
        "INDICATOR_LEFT\n"
        "INDICATOR_RIGHT\n"
        "FUEL_SENDER_V\n"
        "CHIP_TEMP\n"
        "FPS\n"
        "CPU_PERCENT\n"
        "FREE_HEAP_KB\n"
        "FREE_PSRAM_KB\n"
        "UPTIME_S\n"
        "WIFI_RSSI\n"
        "RPM\n"
        "VEHICLE_SPEED\n"
        "THROTTLE\n"
        "COOLANT_TEMP");
    lv_obj_set_size(sig_dd, 240, 32);
    lv_obj_align(sig_dd, LV_ALIGN_TOP_LEFT, 0, 16);
    _style_dropdown(sig_dd);
    lv_dropdown_set_selected(sig_dd, 0);  /* INDICATOR_LEFT default */
    lv_obj_add_event_cb(sig_dd, _dimmer_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *thr_cap = lv_label_create(body);
    lv_label_set_text(thr_cap, "Threshold");
    lv_obj_align(thr_cap, LV_ALIGN_TOP_LEFT, 260, 0);
    lv_obj_set_style_text_font(thr_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(thr_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *thr_ta = lv_textarea_create(body);
    lv_textarea_set_one_line(thr_ta, true);
    lv_textarea_set_max_length(thr_ta, 8);
    lv_textarea_set_text(thr_ta, "0.5");
    lv_obj_set_size(thr_ta, 160, 32);
    lv_obj_align(thr_ta, LV_ALIGN_TOP_LEFT, 260, 16);
    lv_obj_set_style_bg_color(thr_ta, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_text_color(thr_ta, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(thr_ta, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(thr_ta, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(thr_ta, 1, 0);
    lv_obj_set_style_radius(thr_ta, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(thr_ta, 6, 0);

    /* Row 2 — Toggle Mode */
    lv_obj_t *mode_cap = lv_label_create(body);
    lv_label_set_text(mode_cap, "Toggle Mode");
    lv_obj_align(mode_cap, LV_ALIGN_TOP_LEFT, 0, 62);
    lv_obj_set_style_text_font(mode_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(mode_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *mode_dd = lv_dropdown_create(body);
    lv_dropdown_set_options(mode_dd, "On/Off\nMomentary");
    lv_obj_set_size(mode_dd, 240, 32);
    lv_obj_align(mode_dd, LV_ALIGN_TOP_LEFT, 0, 78);
    _style_dropdown(mode_dd);
    lv_dropdown_set_selected(mode_dd, 0);
    lv_obj_add_event_cb(mode_dd, _dimmer_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Row 2 right — Invert + Enabled switches side-by-side */
    lv_obj_t *inv_cap = lv_label_create(body);
    lv_label_set_text(inv_cap, "Invert");
    lv_obj_align(inv_cap, LV_ALIGN_TOP_LEFT, 260, 62);
    lv_obj_set_style_text_font(inv_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(inv_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *inv_sw = lv_switch_create(body);
    lv_obj_set_size(inv_sw, 46, 24);
    lv_obj_align(inv_sw, LV_ALIGN_TOP_LEFT, 260, 82);
    lv_obj_set_style_bg_color(inv_sw, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_color(inv_sw, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_t *en_cap = lv_label_create(body);
    lv_label_set_text(en_cap, "Enabled");
    lv_obj_align(en_cap, LV_ALIGN_TOP_LEFT, 340, 62);
    lv_obj_set_style_text_font(en_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(en_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *en_sw = lv_switch_create(body);
    lv_obj_set_size(en_sw, 46, 24);
    lv_obj_align(en_sw, LV_ALIGN_TOP_LEFT, 340, 82);
    lv_obj_set_style_bg_color(en_sw, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_color(en_sw, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(en_sw, LV_STATE_CHECKED);  /* enabled by default */

    /* Row 3 — Dim Brightness slider + live %-label */
    lv_obj_t *dim_cap = lv_label_create(body);
    lv_label_set_text(dim_cap, "Dim Brightness");
    lv_obj_align(dim_cap, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_style_text_font(dim_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(dim_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *dim_slider = lv_slider_create(body);
    lv_obj_set_size(dim_slider, 340, 20);
    lv_obj_align(dim_slider, LV_ALIGN_TOP_LEFT, 0, 150);
    lv_slider_set_range(dim_slider, 5, 100);
    lv_slider_set_value(dim_slider, 40, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(dim_slider, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(dim_slider, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dim_slider, 10, 0);
    lv_obj_set_style_bg_color(dim_slider, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(dim_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(dim_slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(dim_slider, THEME_COLOR_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_radius(dim_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    lv_obj_t *dim_val = lv_label_create(body);
    lv_label_set_text(dim_val, "40%");
    lv_obj_align(dim_val, LV_ALIGN_TOP_LEFT, 350, 152);
    lv_obj_set_style_text_font(dim_val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(dim_val, THEME_COLOR_TEXT_PRIMARY, 0);
    /* Keep the %-label in sync as the user drags the slider. */
    lv_obj_add_event_cb(dim_slider, _dimmer_slider_cb, LV_EVENT_VALUE_CHANGED, dim_val);

    /* Row 4 — Explanation */
    lv_obj_t *explain = lv_label_create(body);
    lv_label_set_text(explain,
        "When the selected signal crosses the threshold, the dash\n"
        "drops to the Dim Brightness above. Invert flips the polarity\n"
        "(active = below threshold). Momentary resets on signal low.");
    lv_obj_align(explain, LV_ALIGN_TOP_LEFT, 0, 184);
    lv_obj_set_style_text_font(explain, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(explain, THEME_COLOR_TEXT_MUTED, 0);

    /* Footer: Save + Cancel (popup is closed on backdrop click too) */
    lv_obj_t *footer = lv_obj_create(popup);
    lv_obj_set_size(footer, lv_pct(100), 52);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(footer, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_pad_all(footer, 10, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = lv_btn_create(footer);
    lv_obj_set_size(cancel, 200, 32);
    lv_obj_align(cancel, LV_ALIGN_LEFT_MID, 14, 0);
    _style_secondary_btn(cancel);
    lv_obj_t *cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(cancel_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(cancel, _dimmer_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save = lv_btn_create(footer);
    lv_obj_set_size(save, 200, 32);
    lv_obj_align(save, LV_ALIGN_RIGHT_MID, -14, 0);
    _style_accent_btn(save);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  Save");
    lv_obj_center(save_lbl);
    lv_obj_set_style_text_font(save_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(save_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(save, _dimmer_save_cb, LV_EVENT_CLICKED, NULL);
}

void dimmer_config_sandbox_close(void) { _dimmer_close(); }

EMSCRIPTEN_KEEPALIVE
void sandbox_open_dimmer_config(void) { dimmer_config_sandbox_open(); }

/* ─────────────────────────────────────────────────────────────────────
 *  CAN Bus Scan overlay
 *
 *  Opens over whatever screen is active. Sweeps the four common
 *  bitrates with a fake progress bar (same mock data as the wizard's
 *  first-boot scan — 500 kbps wins). Completion shows Apply + Close
 *  so the user has a full loop to experiment with. UI-only: tapping
 *  Apply doesn't mutate any real bus config — the sandbox has no
 *  CAN bus to bind to.
 * ───────────────────────────────────────────────────────────────────── */

static lv_obj_t  *s_scan_overlay   = NULL;
static lv_obj_t  *s_scan_status    = NULL;
static lv_obj_t  *s_scan_bar       = NULL;
static lv_obj_t  *s_scan_rows[4]   = {NULL};
static lv_obj_t  *s_scan_detail    = NULL;
static lv_obj_t  *s_scan_apply_btn = NULL;
static lv_timer_t *s_scan_timer    = NULL;
static uint8_t    s_scan_phase     = 0;

static void _bus_scan_close(void) {
    if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    lv_obj_t *o = s_scan_overlay;
    s_scan_overlay = NULL;
    s_scan_status = s_scan_bar = s_scan_detail = s_scan_apply_btn = NULL;
    for (int i = 0; i < 4; i++) s_scan_rows[i] = NULL;
    if (o && lv_obj_is_valid(o)) lv_obj_del(o);
}

static void _bus_scan_close_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _bus_scan_close();
}

static void _bus_scan_tick(lv_timer_t *t) {
    (void)t;
    if (!s_scan_bar || !lv_obj_is_valid(s_scan_bar)) return;
    s_scan_phase++;

    /* 8 ticks of 250 ms each = 2 seconds. Rows reveal one at a time
     * with fake frame counts — 500 kbps wins with 142 frames. */
    const char *row_text[4] = {
        "125 kbps: no traffic",
        "250 kbps: 2 frames (noise)",
        "500 kbps: 142 frames - 8 IDs",
        "1 Mbps:   no traffic",
    };
    const lv_color_t row_color[4] = {
        THEME_COLOR_TEXT_MUTED,
        THEME_COLOR_TEXT_MUTED,
        THEME_COLOR_ACCENT_GREEN,
        THEME_COLOR_TEXT_MUTED,
    };

    uint8_t progress = (s_scan_phase * 100) / 8;
    if (progress > 100) progress = 100;
    lv_bar_set_value(s_scan_bar, progress, LV_ANIM_ON);

    if (s_scan_phase <= 4) {
        uint8_t idx = s_scan_phase - 1;
        if (idx < 4 && s_scan_rows[idx] && lv_obj_is_valid(s_scan_rows[idx])) {
            lv_label_set_text(s_scan_rows[idx], row_text[idx]);
            lv_obj_set_style_text_color(s_scan_rows[idx], row_color[idx], 0);
        }
        if (s_scan_status) {
            lv_label_set_text_fmt(s_scan_status, "Scanning %s...",
                                  (idx < 4) ? (const char *[]){ "125k", "250k", "500k", "1M" }[idx] : "...");
        }
    } else if (s_scan_phase >= 8) {
        /* Complete — stop, swap status text, reveal Apply button. */
        if (s_scan_status) {
            lv_label_set_text(s_scan_status, "Scan complete");
            lv_obj_set_style_text_color(s_scan_status, THEME_COLOR_ACCENT_GREEN, 0);
        }
        if (s_scan_detail) {
            lv_label_set_text(s_scan_detail,
                "Recommended: 500 kbps (142 frames, 8 unique IDs)");
        }
        if (s_scan_apply_btn && lv_obj_is_valid(s_scan_apply_btn))
            lv_obj_clear_flag(s_scan_apply_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    }
}

void bus_scan_sandbox_open(void) {
    if (s_scan_overlay && lv_obj_is_valid(s_scan_overlay)) return;

    s_scan_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_scan_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(s_scan_overlay);
    lv_obj_set_style_bg_color(s_scan_overlay, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scan_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_scan_overlay, 0, 0);
    lv_obj_clear_flag(s_scan_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_scan_overlay);
    lv_obj_set_size(card, 560, 360);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "CAN Bus Scan");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    s_scan_status = lv_label_create(card);
    lv_label_set_text(s_scan_status, "Scanning 125k...");
    lv_obj_align(s_scan_status, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_text_font(s_scan_status, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_scan_status, THEME_COLOR_TEXT_MUTED, 0);

    s_scan_bar = lv_bar_create(card);
    lv_obj_set_size(s_scan_bar, 520, 14);
    lv_obj_align(s_scan_bar, LV_ALIGN_TOP_LEFT, 0, 54);
    lv_bar_set_range(s_scan_bar, 0, 100);
    lv_bar_set_value(s_scan_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_scan_bar, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(s_scan_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_scan_bar, 7, 0);
    lv_obj_set_style_bg_color(s_scan_bar, THEME_COLOR_ACCENT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_scan_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_scan_bar, 7, LV_PART_INDICATOR);

    for (int i = 0; i < 4; i++) {
        s_scan_rows[i] = lv_label_create(card);
        const char *pre[4] = {
            "125 kbps: --",
            "250 kbps: --",
            "500 kbps: --",
            "1 Mbps:   --",
        };
        lv_label_set_text(s_scan_rows[i], pre[i]);
        lv_obj_align(s_scan_rows[i], LV_ALIGN_TOP_LEFT, 0, 82 + i * 22);
        lv_obj_set_style_text_font(s_scan_rows[i], THEME_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_scan_rows[i], THEME_COLOR_TEXT_HINT, 0);
    }

    s_scan_detail = lv_label_create(card);
    lv_label_set_text(s_scan_detail, "Testing each bitrate for 500 ms. Sit tight.");
    lv_obj_align(s_scan_detail, LV_ALIGN_TOP_LEFT, 0, 186);
    lv_obj_set_style_text_font(s_scan_detail, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_scan_detail, THEME_COLOR_TEXT_MUTED, 0);

    /* Close button — always visible */
    lv_obj_t *close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 240, 36);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    _style_secondary_btn(close_btn);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(close_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(close_btn, _bus_scan_close_cb, LV_EVENT_CLICKED, NULL);

    /* Apply button — hidden until scan completes */
    s_scan_apply_btn = lv_btn_create(card);
    lv_obj_set_size(s_scan_apply_btn, 240, 36);
    lv_obj_align(s_scan_apply_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    _style_accent_btn(s_scan_apply_btn);
    lv_obj_t *apply_lbl = lv_label_create(s_scan_apply_btn);
    lv_label_set_text(apply_lbl, LV_SYMBOL_OK "  Apply 500 kbps");
    lv_obj_center(apply_lbl);
    lv_obj_set_style_text_font(apply_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(apply_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_flag(s_scan_apply_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scan_apply_btn, _bus_scan_close_cb, LV_EVENT_CLICKED, NULL);

    s_scan_phase = 0;
    if (s_scan_timer) lv_timer_del(s_scan_timer);
    s_scan_timer = lv_timer_create(_bus_scan_tick, 250, NULL);
}

void bus_scan_sandbox_close(void) { _bus_scan_close(); }

EMSCRIPTEN_KEEPALIVE
void sandbox_open_bus_scan(void) { bus_scan_sandbox_open(); }
