/* ui.h — Stub for firmware UI declarations (WASM build) */
#pragma once
#include "lvgl.h"

/* Custom font forward declarations — compiled bitmap fonts from firmware */
extern const lv_font_t ui_font_fugaz_14;
extern const lv_font_t ui_font_fugaz_17;
extern const lv_font_t ui_font_fugaz_28;
extern const lv_font_t ui_font_fugaz_56;
extern const lv_font_t ui_font_Manrope_35_BOLD;
extern const lv_font_t ui_font_Manrope_54_BOLD;

/* Compiled image assets */
LV_IMG_DECLARE(ui_img_indicator_left_png);
LV_IMG_DECLARE(ui_img_indicator_right_png);

/* Indicator LVGL object globals (stubs) */
extern lv_obj_t *ui_Indicator_Left;
extern lv_obj_t *ui_Indicator_Right;

/* Initialize font stubs (call before any widget creation) */
void font_stubs_init(void);
