/* pointer_injector.c — virtual LVGL pointer indev driven from JavaScript.
 *
 * Same pattern as the firmware's main/system/remote_touch.c: we register
 * an LV_INDEV_TYPE_POINTER and feed it coordinates set via
 * sandbox_set_pointer(x, y, pressed). The deferred-release trick lets
 * the JS side fire a down+up within a single animation frame (faster
 * than LVGL's ~30 Hz polling) without LVGL missing the press.
 *
 * This is what makes Guided Tours work: the tutorial-runner in JS
 * schedules scripted clicks with millisecond precision, and LVGL always
 * sees a clean PR→REL sequence.
 */
#include "esp_idf_shim.h"
#include "lvgl.h"
#include <stdbool.h>

static bool     s_pressed           = false;
static bool     s_release_requested = false;
static int16_t  s_x = 0;
static int16_t  s_y = 0;

static lv_indev_drv_t s_drv;

static void _read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (s_pressed) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = s_x;
        data->point.y = s_y;
        if (s_release_requested) {
            /* LVGL has seen at least one PR — safe to release next poll */
            s_pressed           = false;
            s_release_requested = false;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = s_x;
        data->point.y = s_y;
    }
}

void pointer_injector_init(void) {
    lv_indev_drv_init(&s_drv);
    s_drv.type    = LV_INDEV_TYPE_POINTER;
    s_drv.read_cb = _read_cb;
    lv_indev_drv_register(&s_drv);
    ESP_LOGI("pointer_inj", "Virtual pointer indev registered");
}

void pointer_injector_set(int16_t x, int16_t y, bool pressed) {
    s_x = x;
    s_y = y;
    if (pressed) {
        s_pressed           = true;
        s_release_requested = false;
    } else {
        /* Defer release so a same-frame down+up still produces a click */
        s_release_requested = true;
    }
}
