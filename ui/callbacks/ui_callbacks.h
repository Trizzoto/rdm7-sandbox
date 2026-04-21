/* ui/callbacks/ui_callbacks.h — WASM stub */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *keyboard;

static inline void keyboard_event_cb(lv_event_t *e) { (void)e; }
static inline void keyboard_ready_event_cb(lv_event_t *e) { (void)e; }

#ifdef __cplusplus
}
#endif
