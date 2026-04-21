/* ui_stubs.c — definitions for UI globals (WASM stub) */
#include "lvgl.h"

/* RPM configuration globals — must match firmware defaults */
int rpm_gauge_max = 7000;
int rpm_redline_value = 6000;

/* dashboard.h globals */
lv_obj_t *ui_Screen3 = NULL;
lv_obj_t *ui_Bar_1 = NULL;
lv_obj_t *ui_Bar_2 = NULL;
lv_obj_t *ui_Bar_1_Label = NULL;
lv_obj_t *ui_Bar_2_Label = NULL;
lv_obj_t *ui_Box[8] = {0};
lv_obj_t *ui_Label[8] = {0};
lv_obj_t *ui_Value[8] = {0};
lv_obj_t *ui_CustomText[8] = {0};

/* menu_screen.h globals */
lv_obj_t *ui_MenuScreen = NULL;
lv_obj_t *menu_bar_objects[2] = {0};
lv_obj_t *config_bars[2] = {0};
lv_obj_t *menu_panel_value_labels[8] = {0};
lv_obj_t *menu_panel_boxes[8] = {0};

/* ui_callbacks.h globals */
lv_obj_t *keyboard = NULL;

/* indicator widget globals */
lv_obj_t *ui_Indicator_Left = NULL;
lv_obj_t *ui_Indicator_Right = NULL;

/* warning widget globals */
lv_obj_t *ui_Warning_1 = NULL;
lv_obj_t *ui_Warning_2 = NULL;
lv_obj_t *ui_Warning_3 = NULL;
lv_obj_t *ui_Warning_4 = NULL;
lv_obj_t *ui_Warning_5 = NULL;
lv_obj_t *ui_Warning_6 = NULL;
lv_obj_t *ui_Warning_7 = NULL;
lv_obj_t *ui_Warning_8 = NULL;
