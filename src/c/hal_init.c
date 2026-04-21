/* hal_init.c — LVGL v8 SDL2 display driver for Emscripten */
#include <emscripten.h>
#include <stdlib.h>
#include "lvgl.h"
#include "sdl/sdl.h"

#define DISP_HOR_RES_DEFAULT 800
#define DISP_VER_RES_DEFAULT 480
#define DISP_MAX_PIXELS      (720 * 720)  /* Largest supported preset */

static lv_disp_draw_buf_t disp_buf;
static lv_color_t *draw_buf = NULL;
static lv_disp_drv_t disp_drv;

void hal_init(void) {
    /* Initialize SDL (creates the window/canvas) */
    sdl_init();

    /* Allocate draw buffer for max resolution */
    draw_buf = (lv_color_t *)malloc(sizeof(lv_color_t) * DISP_MAX_PIXELS);
    lv_disp_draw_buf_init(&disp_buf, draw_buf, NULL, DISP_MAX_PIXELS);

    /* Display driver — full framebuffer mode */
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.flush_cb     = sdl_display_flush;
    disp_drv.hor_res      = DISP_HOR_RES_DEFAULT;
    disp_drv.ver_res      = DISP_VER_RES_DEFAULT;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    /* Mouse input (for testing) */
    static lv_indev_drv_t mouse_drv;
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type    = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&mouse_drv);
}

/* Dynamic resize removed for the sandbox — tutorials always run at
 * 800x480 to match the tour's click coordinates. */
