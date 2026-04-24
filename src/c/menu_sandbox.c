/* menu_sandbox.c — dashboard menu button + Setup Menu modal.
 *
 * Mirrors the real firmware's flow in ui_Screen3.c:
 *   1. Short tap on dashboard (<300 ms) → show the floating Menu button
 *      for 6 seconds, then auto-hide.
 *   2. Tap the Menu button → open the Setup Menu screen (LAYOUT +
 *      SPLASH dropdowns, Device Settings launcher, Close).
 *   3. Device Settings button → device_settings_sandbox_open(...).
 *
 * All chrome colours / sizes / radii are copied verbatim from the
 * firmware's theme.h so the sandbox looks native. The Layout / Splash
 * dropdowns are intentionally inert in this pass (tutorial will just
 * narrate them) — see roadmap. */

#include "lvgl.h"
#include "esp_idf_shim.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ── Shared theme (kept in sync with wizard_sandbox.c) ───────────────── */
#define THEME_COLOR_BG                  lv_color_hex(0x000000)
#define THEME_COLOR_SURFACE             lv_color_hex(0x292C29)
#define THEME_COLOR_SECTION_BG          lv_color_hex(0x393C39)
#define THEME_COLOR_INPUT_BG            lv_color_hex(0x181C18)
#define THEME_COLOR_BORDER              lv_color_hex(0x181C18)
#define THEME_COLOR_TEXT_PRIMARY        lv_color_hex(0xE8E8E8)
#define THEME_COLOR_TEXT_MUTED          lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_ON_ACCENT      lv_color_hex(0xFFFFFF)
#define THEME_COLOR_ACCENT_BLUE         lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_BLUE_PRESSED lv_color_hex(0x1976D2)
#define THEME_COLOR_ACCENT_TEAL         lv_color_hex(0x26C6DA)
#define THEME_FONT_TINY                 (&lv_font_montserrat_10)
#define THEME_FONT_SMALL                (&lv_font_montserrat_12)
#define THEME_FONT_MEDIUM               (&lv_font_montserrat_14)
#define THEME_FONT_LARGE                (&lv_font_montserrat_18)
#define THEME_RADIUS_SMALL              2
#define THEME_RADIUS_NORMAL             4

/* ── Cross-module hooks ──────────────────────────────────────────────── */
/* Owned by wizard_sandbox.c — we attach our button and tap handler to
 * the same screen that's hosting the firmware-rendered default layout. */
extern lv_obj_t *wizard_sandbox_get_dashboard_screen(void);
/* Built in device_settings_sandbox.c. We're its only caller. */
extern void device_settings_sandbox_open(lv_obj_t *return_screen);

/* ── Menu-button state (on the dashboard screen) ─────────────────────── */
static lv_obj_t  *s_menu_button      = NULL;
static lv_timer_t *s_menu_hide_timer = NULL;
static uint32_t   s_touch_press_time = 0;
static bool       s_tap_handler_attached = false;

/* ── Setup Menu screen state ─────────────────────────────────────────── */
static lv_obj_t *s_setup_menu_screen = NULL;

/* ─────────────────────────────────────────────────────────────────────
 *  Menu button
 * ───────────────────────────────────────────────────────────────────── */

static void _menu_hide_timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_menu_button && lv_obj_is_valid(s_menu_button))
        lv_obj_add_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
    if (s_menu_hide_timer) {
        lv_timer_del(s_menu_hide_timer);
        s_menu_hide_timer = NULL;
    }
}

static void _reveal_menu_button(void) {
    if (!s_menu_button || !lv_obj_is_valid(s_menu_button)) return;
    lv_obj_clear_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_menu_button);
    if (s_menu_hide_timer) {
        lv_timer_del(s_menu_hide_timer);
        s_menu_hide_timer = NULL;
    }
    s_menu_hide_timer = lv_timer_create(_menu_hide_timer_cb, 6000, NULL);
    lv_timer_set_repeat_count(s_menu_hide_timer, 1);
}

/* Matches the firmware's screen3_touch_event_cb: PRESSED timestamps,
 * RELEASED within 300 ms counts as a short tap and pops the button. */
static void _dashboard_touch_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_touch_press_time = lv_tick_get();
    } else if (code == LV_EVENT_RELEASED) {
        uint32_t dur = lv_tick_get() - s_touch_press_time;
        if (dur < 300) _reveal_menu_button();
        s_touch_press_time = 0;
    }
}

/* Forward decl — setup menu builder. */
static void _open_setup_menu(void);

static void _menu_button_clicked_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_menu_button && lv_obj_is_valid(s_menu_button))
        lv_obj_add_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
    if (s_menu_hide_timer) {
        lv_timer_del(s_menu_hide_timer);
        s_menu_hide_timer = NULL;
    }
    _open_setup_menu();
}

/* Public: build menu button + tap handler on the dashboard screen. Safe
 * to call more than once — re-creates if the previous screen was freed. */
void menu_sandbox_attach_dashboard(lv_obj_t *dash_screen) {
    if (!dash_screen || !lv_obj_is_valid(dash_screen)) return;

    if (!s_tap_handler_attached) {
        lv_obj_add_flag(dash_screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(dash_screen, _dashboard_touch_cb, LV_EVENT_PRESSED,  NULL);
        lv_obj_add_event_cb(dash_screen, _dashboard_touch_cb, LV_EVENT_RELEASED, NULL);
        s_tap_handler_attached = true;
    }

    if (s_menu_button && !lv_obj_is_valid(s_menu_button)) s_menu_button = NULL;
    if (s_menu_button) return;

    s_menu_button = lv_btn_create(dash_screen);
    lv_obj_set_size(s_menu_button, 100, 36);
    lv_obj_align(s_menu_button, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_bg_color(s_menu_button, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_color(s_menu_button, THEME_COLOR_ACCENT_BLUE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_menu_button, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(s_menu_button, 12, 0);
    lv_obj_set_style_shadow_color(s_menu_button, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_menu_button, LV_OPA_50, 0);
    lv_obj_set_style_shadow_spread(s_menu_button, 0, 0);
    lv_obj_set_style_shadow_ofs_y(s_menu_button, 2, 0);
    lv_obj_set_style_border_width(s_menu_button, 0, 0);
    lv_obj_add_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl = lv_label_create(s_menu_button);
    lv_label_set_text(lbl, LV_SYMBOL_SETTINGS "  Menu");
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);

    lv_obj_add_event_cb(s_menu_button, _menu_button_clicked_cb, LV_EVENT_CLICKED, NULL);
}

/* Public: used by the scene dispatcher — jumping to SCENE_DASHBOARD_MENU
 * pops the button up immediately without the user needing to tap. */
void menu_sandbox_show_menu_button(void) {
    _reveal_menu_button();
}

/* Called by the tour runner when rewinding back to the raw dashboard
 * (kills the 6 s timer so it doesn't surprise-hide the button mid-tour). */
void menu_sandbox_hide_menu_button(void) {
    if (s_menu_hide_timer) {
        lv_timer_del(s_menu_hide_timer);
        s_menu_hide_timer = NULL;
    }
    if (s_menu_button && lv_obj_is_valid(s_menu_button))
        lv_obj_add_flag(s_menu_button, LV_OBJ_FLAG_HIDDEN);
}

/* ─────────────────────────────────────────────────────────────────────
 *  Setup Menu screen (Layout / Splash / Device Settings / Close)
 * ───────────────────────────────────────────────────────────────────── */

static void _setup_menu_close_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *scr = s_setup_menu_screen;
    lv_obj_t *dash = wizard_sandbox_get_dashboard_screen();
    s_setup_menu_screen = NULL;
    if (dash && lv_obj_is_valid(dash)) lv_scr_load(dash);
    if (scr && lv_obj_is_valid(scr))   lv_obj_del_async(scr);
}

static void _device_settings_launch_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *scr = s_setup_menu_screen;
    /* Open device settings with the dashboard as its return target —
     * matches firmware's menu_device_settings_cb flow. */
    device_settings_sandbox_open(wizard_sandbox_get_dashboard_screen());
    s_setup_menu_screen = NULL;
    if (scr && lv_obj_is_valid(scr)) lv_obj_del_async(scr);
}

/* Layout / Splash dropdowns are visually present but don't rebuild the
 * dashboard in this pass (scoped out for a later tutorial). We still
 * wire a VALUE_CHANGED handler so the dropdown chrome animates. */
static void _dropdown_noop_cb(lv_event_t *e) {
    (void)e; /* UI-only — no reload until layout-swap support lands. */
}

static void _style_setup_dropdown(lv_obj_t *dd) {
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

static void _open_setup_menu(void) {
    if (s_setup_menu_screen && lv_obj_is_valid(s_setup_menu_screen)) return;

    s_setup_menu_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_setup_menu_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_setup_menu_screen, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_setup_menu_screen, LV_OPA_COVER, 0);

    /* Container — 340×380 modal, centered */
    lv_obj_t *container = lv_obj_create(s_setup_menu_screen);
    lv_obj_set_size(container, 340, 380);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(container, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_radius(container, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *header = lv_obj_create(container);
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
    lv_label_set_text(title, "Menu");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_text_font(title, THEME_FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 60, 28);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, THEME_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(close_lbl, THEME_COLOR_TEXT_MUTED, 0);
    lv_obj_add_event_cb(close_btn, _setup_menu_close_cb, LV_EVENT_CLICKED, NULL);

    /* Content column */
    lv_obj_t *content = lv_obj_create(container);
    lv_obj_set_size(content, lv_pct(100), 328);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_opa(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 14, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* LAYOUT section card */
    lv_obj_t *layout_card = lv_obj_create(content);
    lv_obj_set_size(layout_card, 304, 100);
    lv_obj_align(layout_card, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(layout_card, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(layout_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(layout_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(layout_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(layout_card, 1, 0);
    lv_obj_set_style_pad_all(layout_card, 12, 0);
    lv_obj_clear_flag(layout_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *layout_title = lv_label_create(layout_card);
    lv_label_set_text(layout_title, "LAYOUT");
    lv_obj_align(layout_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(layout_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(layout_title, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_text_letter_space(layout_title, 1, 0);

    lv_obj_t *layout_sub = lv_label_create(layout_card);
    lv_label_set_text(layout_sub, "Active dashboard");
    lv_obj_align(layout_sub, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_text_font(layout_sub, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(layout_sub, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *layout_dd = lv_dropdown_create(layout_card);
    lv_dropdown_set_options(layout_dd, "Default\nRacing");
    lv_obj_set_size(layout_dd, 278, 32);
    lv_obj_align(layout_dd, LV_ALIGN_TOP_LEFT, 0, 40);
    _style_setup_dropdown(layout_dd);
    lv_obj_add_event_cb(layout_dd, _dropdown_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* SPLASH section card */
    lv_obj_t *splash_card = lv_obj_create(content);
    lv_obj_set_size(splash_card, 304, 100);
    lv_obj_align(splash_card, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(splash_card, THEME_COLOR_SECTION_BG, 0);
    lv_obj_set_style_bg_opa(splash_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(splash_card, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_border_color(splash_card, THEME_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(splash_card, 1, 0);
    lv_obj_set_style_pad_all(splash_card, 12, 0);
    lv_obj_clear_flag(splash_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *splash_title = lv_label_create(splash_card);
    lv_label_set_text(splash_title, "SPLASH SCREEN");
    lv_obj_align(splash_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(splash_title, THEME_FONT_TINY, 0);
    lv_obj_set_style_text_color(splash_title, THEME_COLOR_ACCENT_TEAL, 0);
    lv_obj_set_style_text_letter_space(splash_title, 1, 0);

    lv_obj_t *splash_sub = lv_label_create(splash_card);
    lv_label_set_text(splash_sub, "Shown on boot");
    lv_obj_align(splash_sub, LV_ALIGN_TOP_LEFT, 0, 18);
    lv_obj_set_style_text_font(splash_sub, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(splash_sub, THEME_COLOR_TEXT_MUTED, 0);

    lv_obj_t *splash_dd = lv_dropdown_create(splash_card);
    lv_dropdown_set_options(splash_dd, "Default");
    lv_obj_set_size(splash_dd, 278, 32);
    lv_obj_align(splash_dd, LV_ALIGN_TOP_LEFT, 0, 40);
    _style_setup_dropdown(splash_dd);
    lv_obj_add_event_cb(splash_dd, _dropdown_noop_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Device Settings launcher — blue accent, full-width row */
    lv_obj_t *ds_btn = lv_btn_create(content);
    lv_obj_set_size(ds_btn, 304, 40);
    lv_obj_align(ds_btn, LV_ALIGN_TOP_MID, 0, 218);
    lv_obj_set_style_bg_color(ds_btn, THEME_COLOR_ACCENT_BLUE, 0);
    lv_obj_set_style_bg_color(ds_btn, THEME_COLOR_ACCENT_BLUE_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(ds_btn, THEME_RADIUS_NORMAL, 0);
    lv_obj_set_style_shadow_width(ds_btn, 0, 0);
    lv_obj_t *ds_lbl = lv_label_create(ds_btn);
    lv_label_set_text(ds_lbl, LV_SYMBOL_SETTINGS "  Device Settings");
    lv_obj_center(ds_lbl);
    lv_obj_set_style_text_font(ds_lbl, THEME_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ds_lbl, THEME_COLOR_TEXT_ON_ACCENT, 0);
    lv_obj_add_event_cb(ds_btn, _device_settings_launch_cb, LV_EVENT_CLICKED, NULL);

    lv_scr_load(s_setup_menu_screen);
}

/* Public entry for the scene dispatcher. */
void menu_sandbox_open_setup_menu(void) {
    _open_setup_menu();
}

/* Public teardown — called when the scene dispatcher leaves the setup
 * menu so we return to the dashboard without leaking the screen. */
void menu_sandbox_close_setup_menu(void) {
    lv_obj_t *scr = s_setup_menu_screen;
    s_setup_menu_screen = NULL;
    if (scr && lv_obj_is_valid(scr)) lv_obj_del(scr);
}
