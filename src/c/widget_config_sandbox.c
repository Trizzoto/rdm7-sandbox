/* widget_config_sandbox.c — Widget-config modal clone.
 *
 * When a user taps any widget on the real dashboard, the firmware
 * opens a tabbed configuration modal (main/ui/menu/config_modal.c,
 * 1926 lines) so they can rebind the signal, change colours, tweak
 * decimals, set alert thresholds, etc. This sandbox module replicates
 * the modal visually — same tabs, same field layout — with inert
 * controls so the in-browser tutorial can demonstrate the workflow.
 *
 * Visuals match firmware's config_modal.c exactly:
 *   590 × 640 centred modal, surface bg, 1px border, shadow
 *   Header (70 px): widget id + type on left, close X on right
 *   Tabbar (40 px): DATA | PRESETS | ALERTS (active tab underlined)
 *   Content area: section cards (DISPLAY, DATA SOURCE, …)
 *   Footer (56 px): CANCEL (left, secondary) | SAVE (right, accent)
 *
 * Only the DATA tab has populated content; the other tabs render
 * their title placeholders so the user sees they exist. Save / Cancel
 * both close the modal — there's no real persistence in the sandbox. */

#include "lvgl.h"
#include "esp_idf_shim.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Theme (kept in sync with the other sandbox modules) ─────────────── */
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
#define THEME_FONT_TINY                 (&lv_font_montserrat_10)
#define THEME_FONT_SMALL                (&lv_font_montserrat_12)
#define THEME_FONT_MEDIUM               (&lv_font_montserrat_14)
#define THEME_RADIUS_SMALL              2
#define THEME_RADIUS_NORMAL             4

/* Default target so the bare scene call works without parameters. */
#define DEFAULT_WIDGET_ID     "PANEL_3"
#define DEFAULT_WIDGET_TYPE   "Panel"
#define DEFAULT_WIDGET_SIGNAL "COOLANT_TEMP"
#define DEFAULT_WIDGET_LABEL  "COOLANT"

/* ── Module state ────────────────────────────────────────────────────── */
static lv_obj_t *s_modal_screen = NULL;
static lv_obj_t *s_return_screen = NULL;
static lv_obj_t *s_can_collapsible = NULL;
static bool      s_can_expanded = false;

static char s_widget_id[32]     = DEFAULT_WIDGET_ID;
static char s_widget_type[32]   = DEFAULT_WIDGET_TYPE;
static char s_widget_signal[32] = DEFAULT_WIDGET_SIGNAL;
static char s_widget_label[32]  = DEFAULT_WIDGET_LABEL;

/* ─────────────────────────────────────────────────────────────────────
 *  Helpers
 * ───────────────────────────────────────────────────────────────────── */

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

static void _style_textarea(lv_obj_t *ta) {
    lv_obj_set_style_bg_color(ta, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ta, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(ta, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(ta, 6, 0);
}

/* A single "LABEL\nvalue" row in the collapsible CAN settings grid. */
static void _can_grid_pair(lv_obj_t *parent, int16_t x, int16_t y,
                           const char *label_text, const char *value_text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_text_font(lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, value_text);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, x, y + 14);
    lv_obj_set_style_text_font(val, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(val, THEME_COLOR_TEXT_PRIMARY, 0);
}

/* ─────────────────────────────────────────────────────────────────────
 *  Event callbacks
 * ───────────────────────────────────────────────────────────────────── */

static void _close_modal(void) {
    lv_obj_t *scr = s_modal_screen;
    lv_obj_t *ret = s_return_screen;
    s_modal_screen   = NULL;
    s_can_collapsible = NULL;
    s_can_expanded = false;
    if (ret && lv_obj_is_valid(ret)) lv_scr_load(ret);
    if (scr && lv_obj_is_valid(scr)) lv_obj_del_async(scr);
}

static void _close_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _close_modal();
}

static void _save_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _close_modal();  /* Sandbox never persists — Save is a UI-only close */
}

static void _noop_cb(lv_event_t *e) { (void)e; }

static void _can_toggle_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_can_expanded = !s_can_expanded;
    lv_obj_t *tgt = lv_event_get_target(e);
    lv_label_set_text(tgt, s_can_expanded ? LV_SYMBOL_DOWN  "  CAN Bus Settings"
                                          : LV_SYMBOL_RIGHT "  CAN Bus Settings");
    if (s_can_collapsible && lv_obj_is_valid(s_can_collapsible)) {
        if (s_can_expanded) lv_obj_clear_flag(s_can_collapsible, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(s_can_collapsible, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ─────────────────────────────────────────────────────────────────────
 *  DATA tab builder (the only populated tab)
 * ───────────────────────────────────────────────────────────────────── */

static void _build_data_tab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_pad_row(tab, 10, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);

    /* ── DISPLAY card ──────────────────────────────────────────── */
    lv_obj_t *disp_card = lv_obj_create(tab);
    lv_obj_set_size(disp_card, lv_pct(100), 130);
    lv_obj_set_style_bg_color(disp_card, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(disp_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(disp_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(disp_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(disp_card, 1, 0);
    lv_obj_set_style_pad_all(disp_card, 14, 0);
    lv_obj_clear_flag(disp_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *disp_title = lv_label_create(disp_card);
    lv_label_set_text(disp_title, "DISPLAY");
    lv_obj_align(disp_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(disp_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(disp_title, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_letter_space(disp_title, 1, 0);

    /* Label input */
    lv_obj_t *lbl_cap = lv_label_create(disp_card);
    lv_label_set_text(lbl_cap, "Label");
    lv_obj_align(lbl_cap, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_style_text_font(lbl_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *lbl_input = lv_textarea_create(disp_card);
    lv_textarea_set_one_line(lbl_input, true);
    lv_textarea_set_text(lbl_input, s_widget_label);
    lv_obj_set_size(lbl_input, 280, 30);
    lv_obj_align(lbl_input, LV_ALIGN_TOP_LEFT, 0, 38);
    _style_textarea(lbl_input);

    /* Decimals dropdown */
    lv_obj_t *dec_cap = lv_label_create(disp_card);
    lv_label_set_text(dec_cap, "Decimals");
    lv_obj_align(dec_cap, LV_ALIGN_TOP_LEFT, 300, 20);
    lv_obj_set_style_text_font(dec_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(dec_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *dec_dd = lv_dropdown_create(disp_card);
    lv_dropdown_set_options(dec_dd, "0\n1\n2\n3\n4");
    lv_obj_set_size(dec_dd, 110, 30);
    lv_obj_align(dec_dd, LV_ALIGN_TOP_LEFT, 300, 38);
    _style_dropdown(dec_dd);
    lv_dropdown_set_selected(dec_dd, 1);  /* 1 decimal default for temps */
    lv_obj_add_event_cb(dec_dd, _noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Help text */
    lv_obj_t *help = lv_label_create(disp_card);
    lv_label_set_text(help, "Shown above the numeric value on the dash.");
    lv_obj_align(help, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_obj_set_style_text_font(help, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(help, THEME_COLOR_TEXT_MUTED, 0);

    /* ── DATA SOURCE card ──────────────────────────────────────── */
    lv_obj_t *src_card = lv_obj_create(tab);
    lv_obj_set_size(src_card, lv_pct(100), 220);
    lv_obj_set_style_bg_color(src_card, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(src_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(src_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(src_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(src_card, 1, 0);
    lv_obj_set_style_pad_all(src_card, 14, 0);
    lv_obj_clear_flag(src_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *src_title = lv_label_create(src_card);
    lv_label_set_text(src_title, "DATA SOURCE");
    lv_obj_align(src_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(src_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(src_title, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_letter_space(src_title, 1, 0);

    /* Signal dropdown */
    lv_obj_t *sig_cap = lv_label_create(src_card);
    lv_label_set_text(sig_cap, "Signal");
    lv_obj_align(sig_cap, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_style_text_font(sig_cap, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sig_cap, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *sig_dd = lv_dropdown_create(src_card);
    lv_dropdown_set_options(sig_dd,
        "RPM\n"
        "MAP\n"
        "THROTTLE\n"
        "COOLANT_TEMP\n"
        "INTAKE_AIR_TEMP\n"
        "LAMBDA\n"
        "OIL_TEMP\n"
        "OIL_PRESSURE\n"
        "FUEL_PRESSURE\n"
        "IGNITION\n"
        "VEHICLE_SPEED\n"
        "GEAR\n"
        "BATTERY_VOLTAGE\n"
        "FUEL_TRIM");
    lv_obj_set_size(sig_dd, lv_pct(100), 30);
    lv_obj_align(sig_dd, LV_ALIGN_TOP_LEFT, 0, 38);
    _style_dropdown(sig_dd);
    /* Preselect COOLANT_TEMP (index 3 in the list above) to match the
     * default widget context. */
    lv_dropdown_set_selected(sig_dd, 3);
    lv_obj_add_event_cb(sig_dd, _noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Signal source hint */
    lv_obj_t *sig_hint = lv_label_create(src_card);
    lv_label_set_text(sig_hint, "From active layout - 14 signals available.");
    lv_obj_align(sig_hint, LV_ALIGN_TOP_LEFT, 0, 74);
    lv_obj_set_style_text_font(sig_hint, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(sig_hint, THEME_COLOR_TEXT_MUTED, 0);

    /* Collapsible CAN Bus Settings toggle */
    lv_obj_t *can_toggle = lv_label_create(src_card);
    lv_label_set_text(can_toggle, LV_SYMBOL_RIGHT "  CAN Bus Settings");
    lv_obj_align(can_toggle, LV_ALIGN_TOP_LEFT, 0, 98);
    lv_obj_set_style_text_font(can_toggle, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(can_toggle, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_add_flag(can_toggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(can_toggle, _can_toggle_cb, LV_EVENT_CLICKED, NULL);

    /* Collapsible grid — 3 columns × 2 rows */
    s_can_collapsible = lv_obj_create(src_card);
    lv_obj_set_size(s_can_collapsible, lv_pct(100), 70);
    lv_obj_align(s_can_collapsible, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_obj_set_style_bg_color(s_can_collapsible, THEME_COLOR_INPUT_BG, 0);
    lv_obj_set_style_bg_opa(s_can_collapsible, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_can_collapsible, THEME_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(s_can_collapsible, 0, 0);
    lv_obj_set_style_pad_all(s_can_collapsible, 8, 0);
    lv_obj_clear_flag(s_can_collapsible, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_can_collapsible, LV_OBJ_FLAG_HIDDEN);  /* collapsed by default */
    s_can_expanded = false;

    _can_grid_pair(s_can_collapsible,   0,  0, "CAN ID",     "0x360");
    _can_grid_pair(s_can_collapsible, 140,  0, "Endian",     "Big");
    _can_grid_pair(s_can_collapsible, 280,  0, "Data Type",  "Unsigned");
    _can_grid_pair(s_can_collapsible,   0, 32, "Bit Start",  "16");
    _can_grid_pair(s_can_collapsible, 140, 32, "Bit Length", "16");
    _can_grid_pair(s_can_collapsible, 280, 32, "Scale/Offs", "0.1 / -40");
}

/* PRESETS tab — placeholder (real firmware has a preset signal
 * picker; the sandbox shows a short explainer so the tab isn't empty). */
static void _build_presets_tab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 20, 0);
    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Signal Presets");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *body = lv_label_create(tab);
    lv_label_set_text(body,
        "Pre-built signal templates for Haltech, Link, MoTeC, MaxxECU,\n"
        "MS3-Pro, and Ford Falcon. Each preset carries its own CAN\n"
        "decode so you can bind any widget to any signal without\n"
        "typing bit positions. The preset pulls from the ECU you\n"
        "picked during first-boot, but you can override it here.");
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(body, THEME_COLOR_TEXT_MUTED, 0);
}

/* ALERTS tab — placeholder for high / low threshold config. */
static void _build_alerts_tab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 20, 0);
    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Alert Thresholds");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *body = lv_label_create(tab);
    lv_label_set_text(body,
        "Set a high and low threshold for this signal. When the value\n"
        "crosses either, the widget's colour flips to warning red and\n"
        "(optionally) the label flashes. Thresholds are per-widget so\n"
        "you can run multiple limits on the same signal.");
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(body, THEME_COLOR_TEXT_MUTED, 0);
}

/* ─────────────────────────────────────────────────────────────────────
 *  Public entry
 * ───────────────────────────────────────────────────────────────────── */

/* Build + show the modal on a fresh LVGL screen, remembering the
 * caller's screen so Close / Save / Cancel can return there. */
static void _open_modal(lv_obj_t *return_screen) {
    if (s_modal_screen && lv_obj_is_valid(s_modal_screen)) return;
    s_return_screen = return_screen ? return_screen : lv_scr_act();

    /* Full-screen backdrop — semi-opaque to dim the dashboard behind. */
    s_modal_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_modal_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_modal_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_modal_screen, LV_OPA_COVER, 0);

    /* Modal container */
    lv_obj_t *modal = lv_obj_create(s_modal_screen);
    lv_obj_set_size(modal, 590, 440);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(modal, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(modal, 1, 0);
    lv_obj_set_style_radius(modal, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(modal, 20, 0);
    lv_obj_set_style_shadow_color(modal, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(modal, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header ─────────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(modal);
    lv_obj_set_size(header, lv_pct(100), 54);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(header, 14, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *id_lbl = lv_label_create(header);
    lv_label_set_text(id_lbl, s_widget_id);
    lv_obj_align(id_lbl, LV_ALIGN_LEFT_MID, 0, -6);
    lv_obj_set_style_text_font(id_lbl, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(id_lbl, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *type_lbl = lv_label_create(header);
    lv_label_set_text_fmt(type_lbl, "%s widget", s_widget_type);
    lv_obj_align(type_lbl, LV_ALIGN_LEFT_MID, 0, 10);
    lv_obj_set_style_text_font(type_lbl, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(type_lbl, THEME_COLOR_TEXT_HINT, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 32, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, THEME_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(close_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(close_btn, _close_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ── Tabview ────────────────────────────────────────────────── */
    lv_obj_t *tv = lv_tabview_create(modal, LV_DIR_TOP, 36);
    lv_obj_set_size(tv, lv_pct(100), 320);
    lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(tv, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv, 0, 0);

    /* Style the tab buttons — underline-only active, muted inactive */
    lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(btns, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(btns, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_color(btns, THEME_COLOR_TEXT_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(btns, THEME_FONT_SMALL, 0);
    lv_obj_set_style_border_color(btns, THEME_COLOR_ACCENT_BLUE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t *data_tab    = lv_tabview_add_tab(tv, "DATA");
    lv_obj_t *presets_tab = lv_tabview_add_tab(tv, "PRESETS");
    lv_obj_t *alerts_tab  = lv_tabview_add_tab(tv, "ALERTS");
    _build_data_tab(data_tab);
    _build_presets_tab(presets_tab);
    _build_alerts_tab(alerts_tab);

    /* ── Footer buttons ────────────────────────────────────────── */
    lv_obj_t *footer = lv_obj_create(modal);
    lv_obj_set_size(footer, lv_pct(100), 52);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(footer, 10, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel_btn = lv_btn_create(footer);
    lv_obj_set_size(cancel_btn, 270, 32);
    lv_obj_align(cancel_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(cancel_btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(cancel_btn, 1, 0);
    lv_obj_set_style_border_color(cancel_btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "CANCEL");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(cancel_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_letter_space(cancel_lbl, 1, 0);
    lv_obj_add_event_cb(cancel_btn, _close_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_btn = lv_btn_create(footer);
    lv_obj_set_size(save_btn, 270, 32);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(save_btn, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_color(save_btn, THEME_COLOR_ACCENT_BLUE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(save_btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  SAVE");
    lv_obj_center(save_lbl);
    lv_obj_set_style_text_font(save_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(save_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_set_style_text_letter_space(save_lbl, 1, 0);
    lv_obj_add_event_cb(save_btn, _save_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_scr_load(s_modal_screen);
}

/* ── JS bridge ──────────────────────────────────────────────────────── */
EMSCRIPTEN_KEEPALIVE
void sandbox_open_widget_config(void) {
    /* Dashboard is assumed to be the current active screen; we'll
     * return to it on close. */
    _open_modal(lv_scr_act());
}

EMSCRIPTEN_KEEPALIVE
void sandbox_close_widget_config(void) {
    _close_modal();
}

/* Called by the scene dispatcher — hop to SCENE_WIDGET_CONFIG and the
 * modal opens over the dashboard. */
void widget_config_sandbox_open(lv_obj_t *return_screen) {
    _open_modal(return_screen);
}

void widget_config_sandbox_close(void) {
    _close_modal();
}
