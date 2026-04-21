/* widget_rpm_bar.h — RPM bar widget with redline, tick marks, limiter effects */
#pragma once
#include "lvgl.h"
#include "widget_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_RPM_LINES 25

typedef struct {
    int32_t    gauge_max;
    int32_t    redline;
    lv_color_t bar_color;
    uint8_t    limiter_effect;
    int32_t    limiter_value;
    lv_color_t limiter_color;
    char       signal_name[32];
    int16_t    signal_index;
    /* Runtime LVGL objects */
    lv_obj_t  *bar_obj;
    lv_obj_t  *redline_obj;
    lv_obj_t  *color_panel_obj;
    lv_timer_t *limiter_timer;
    bool       limiter_flash_state;
    int32_t    current_rpm;
    /* Cached bar geometry (set at create time) */
    lv_coord_t bar_x_pos;
    lv_coord_t bar_w_px;
    /* Tick marks */
    lv_obj_t  *tick_lines[MAX_RPM_LINES * 2];
    lv_obj_t  *tick_labels[MAX_RPM_LINES];
    int        num_ticks;
    int        num_labels;
} rpm_bar_data_t;

widget_t *widget_rpm_bar_create_instance(void);

#ifdef __cplusplus
}
#endif
