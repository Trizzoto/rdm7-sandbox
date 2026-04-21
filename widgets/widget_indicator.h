/* widget_indicator.h — Turn signal indicator widget (max 2) */
#pragma once
#include "lvgl.h"
#include "widget_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t    slot;           /* 0 = left, 1 = right */
    uint8_t    input_source;   /* 0 = wire, 1 = CAN */
    bool       animation_enabled; /* default: true */
    bool       is_momentary;   /* default: true */
    char       signal_name[32];
    int16_t    signal_index;
    bool       current_state;  /* runtime */
    lv_obj_t  *indicator_obj;
    lv_timer_t *anim_timer;
    bool       anim_on;
} indicator_data_t;

widget_t *widget_indicator_create_instance(uint8_t slot);

#ifdef __cplusplus
}
#endif
