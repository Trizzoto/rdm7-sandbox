/* ui/dashboard.h — WASM stub for dashboard globals */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *ui_Screen3;
extern lv_obj_t *ui_Bar_1;
extern lv_obj_t *ui_Bar_2;
extern lv_obj_t *ui_Bar_1_Label;
extern lv_obj_t *ui_Bar_2_Label;
extern lv_obj_t *ui_Box[8];
extern lv_obj_t *ui_Label[8];
extern lv_obj_t *ui_Value[8];
extern lv_obj_t *ui_CustomText[8];

#define BAR1_VALUE_ID 1
#define BAR2_VALUE_ID 2

static inline void screen3_touch_event_cb(lv_event_t *e) { (void)e; }

#ifdef __cplusplus
}
#endif
