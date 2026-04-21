/**
 * theme.h — Centralised design tokens for the RDM-7 dashboard.
 * Copied from firmware_src/ui/theme.h with #include "lvgl.h" added at top.
 */
#pragma once
#include "lvgl.h"

/* =========================================================================
 * COLOURS — backgrounds
 * ========================================================================= */
#define THEME_COLOR_BG                  lv_color_hex(0x000000)
#define THEME_COLOR_SURFACE             lv_color_hex(0x292C29)
#define THEME_COLOR_SURFACE_ALT         lv_color_hex(0x292C29)
#define THEME_COLOR_INPUT_BG            lv_color_hex(0x181C18)
#define THEME_COLOR_SECTION_BG          lv_color_hex(0x393C39)
#define THEME_COLOR_INACTIVE            lv_color_hex(0x292C29)
#define THEME_COLOR_PANEL               lv_color_hex(0x393C39)
#define THEME_COLOR_KEYBOARD_BG         lv_color_hex(0x393C39)
#define THEME_COLOR_CONTROL_BG          lv_color_hex(0x393C39)
#define THEME_COLOR_BORDER              lv_color_hex(0x181C18)
#define THEME_COLOR_BTN_NEUTRAL         lv_color_hex(0x393C39)
#define THEME_COLOR_SCROLLBAR           lv_color_hex(0x5A595A)
#define THEME_COLOR_BORDER_MED          lv_color_hex(0x848684)
#define THEME_COLOR_RPM_BAR_BG          lv_color_hex(0xF0F0F0)
#define THEME_COLOR_HIGHLIGHT           lv_color_hex(0x393C39)

/* =========================================================================
 * COLOURS — text
 * ========================================================================= */
#define THEME_COLOR_TEXT_ON_LIGHT       lv_color_hex(0x000000)
#define THEME_COLOR_TEXT_ON_ACCENT      lv_color_hex(0xFFFFFF)
#define THEME_COLOR_TEXT_GHOST          lv_color_hex(0x5A595A)
#define THEME_COLOR_TEXT_HINT           lv_color_hex(0x5A595A)
#define THEME_COLOR_TEXT_DISABLED       lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_MUTED          lv_color_hex(0x848684)
#define THEME_COLOR_TEXT_PRIMARY        lv_color_hex(0xE8E8E8)

/* =========================================================================
 * COLOURS — interactive buttons
 * ========================================================================= */
#define THEME_COLOR_BTN_SAVE            lv_color_hex(0x2196F3)
#define THEME_COLOR_BTN_SAVE_PRESSED    lv_color_hex(0x42A5F5)
#define THEME_COLOR_BTN_CANCEL          lv_color_hex(0xF04030)
#define THEME_COLOR_BTN_CANCEL_PRESSED  lv_color_hex(0xE85050)
#define THEME_COLOR_BTN_CLOSE           lv_color_hex(0xF84040)
#define THEME_COLOR_BTN_CLOSE_PRESSED   lv_color_hex(0xF86868)
#define THEME_COLOR_BTN_SAVE_ALT        lv_color_hex(0x2196F3)
#define THEME_COLOR_BTN_SAVE_ALT_PRESSED lv_color_hex(0x42A5F5)
#define THEME_COLOR_BTN_DIM             lv_color_hex(0x393C39)
#define THEME_COLOR_BTN_DIM_PRESSED     lv_color_hex(0x5A595A)
#define THEME_COLOR_BTN_CONNECT         lv_color_hex(0x2196F3)
#define THEME_COLOR_BTN_CONNECT_PRESSED lv_color_hex(0x42A5F5)
#define THEME_COLOR_BTN_GRAY            lv_color_hex(0x5A595A)
#define THEME_COLOR_BTN_GRAY_PRESSED    lv_color_hex(0x848684)
#define THEME_COLOR_BTN_DANGER          lv_color_hex(0x880000)
#define THEME_COLOR_BTN_DANGER_BG       lv_color_hex(0x301010)

/* =========================================================================
 * COLOURS — status / accent
 * ========================================================================= */
#define THEME_COLOR_STATUS_CONNECTED    lv_color_hex(0x2196F3)
#define THEME_COLOR_STATUS_ERROR        lv_color_hex(0xF84040)
#define THEME_COLOR_STATUS_WARN         lv_color_hex(0xF84080)
#define THEME_COLOR_ACCENT_BLUE         lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_BLUE_PRESSED lv_color_hex(0x42A5F5)
#define THEME_COLOR_ACCENT_YELLOW       lv_color_hex(0xF8FC40)
#define THEME_COLOR_ACCENT_ORANGE       lv_color_hex(0xF88040)
#define THEME_COLOR_ACCENT              lv_color_hex(0x2196F3)
#define THEME_COLOR_ACCENT_DIM          lv_color_hex(0x0D47A1)
#define THEME_COLOR_ACCENT_AMBER        lv_color_hex(0xC89830)
#define THEME_COLOR_ACCENT_TEAL         lv_color_hex(0x218E8C)
#define THEME_COLOR_NAV_DEFAULT         lv_color_hex(0x5A595A)
#define THEME_COLOR_NAV_PRESSED         lv_color_hex(0x2196F3)
#define THEME_COLOR_CHART_BORDER        lv_color_hex(0x848684)

/* =========================================================================
 * COLOURS — section titles
 * ========================================================================= */
#define THEME_COLOR_SECTION_CAN_TITLE   lv_color_hex(0x2196F3)
#define THEME_COLOR_SECTION_INFO_TITLE  lv_color_hex(0x2196F3)
#define THEME_COLOR_SECTION_NET_TITLE   lv_color_hex(0xF88040)
#define THEME_COLOR_SECTION_DISP_TITLE  lv_color_hex(0xF8FC40)
#define THEME_COLOR_SECTION_ECU_TITLE   lv_color_hex(0xF84080)

/* =========================================================================
 * COLOURS — widget palette
 * ========================================================================= */
#define THEME_COLOR_GREEN               lv_color_hex(0x00F800)
#define THEME_COLOR_GREEN_BRIGHT        lv_color_hex(0x38F800)
#define THEME_COLOR_CYAN                lv_color_hex(0x00F8F8)
#define THEME_COLOR_YELLOW              lv_color_hex(0xF8FC00)
#define THEME_COLOR_ORANGE              lv_color_hex(0xF88000)
#define THEME_COLOR_ORANGE_WEB          lv_color_hex(0xF8A400)
#define THEME_COLOR_RED                 lv_color_hex(0xF80000)
#define THEME_COLOR_BLUE                lv_color_hex(0x0080F8)
#define THEME_COLOR_BLUE_DARK           lv_color_hex(0x184098)
#define THEME_COLOR_BLUE_PURE           lv_color_hex(0x0000F8)
#define THEME_COLOR_PURPLE              lv_color_hex(0x8000F8)
#define THEME_COLOR_MAGENTA             lv_color_hex(0xF800F8)
#define THEME_COLOR_PINK                lv_color_hex(0xF81490)

/* =========================================================================
 * FONTS — system
 * ========================================================================= */
#define THEME_FONT_TINY                 (&lv_font_montserrat_10)
#define THEME_FONT_SMALL                (&lv_font_montserrat_12)
#define THEME_FONT_BODY                 (&lv_font_montserrat_14)
#define THEME_FONT_MEDIUM               (&lv_font_montserrat_16)
#define THEME_FONT_LARGE                (&lv_font_montserrat_18)
#define THEME_FONT_XLARGE               (&lv_font_montserrat_20)

/* =========================================================================
 * FONTS — dashboard display
 * ========================================================================= */
#define THEME_FONT_DASH_LABEL           (&ui_font_fugaz_14)
#define THEME_FONT_DASH_TICK            (&ui_font_fugaz_17)
#define THEME_FONT_DASH_RPM             (&ui_font_fugaz_28)
#define THEME_FONT_DASH_SPEED           (&ui_font_fugaz_56)
#define THEME_FONT_DASH_VALUE           (&ui_font_Manrope_35_BOLD)
#define THEME_FONT_DASH_GEAR            (&ui_font_Manrope_54_BOLD)

/* =========================================================================
 * SPACING & GEOMETRY
 * ========================================================================= */
#define THEME_BORDER_W_NONE             0
#define THEME_BORDER_W_THIN             1
#define THEME_BORDER_W_NORMAL           2
#define THEME_RADIUS_NONE               0
#define THEME_RADIUS_SMALL              2
#define THEME_RADIUS_NORMAL             4
#define THEME_RADIUS_LARGE              6
#define THEME_RADIUS_PILL               LV_RADIUS_CIRCLE
#define THEME_PAD_NONE                  0
#define THEME_PAD_TINY                  3
#define THEME_PAD_SMALL                 5
#define THEME_PAD_NORMAL                8
#define THEME_PAD_MEDIUM                12
#define THEME_PAD_LARGE                 20
#define THEME_SHADOW_W_POPUP            20
#define THEME_SHADOW_OFS_POPUP          0
