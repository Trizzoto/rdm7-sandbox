/**
 * lv_conf.h — LVGL v8.3 configuration for WASM preview build.
 * Matches firmware settings from lv_conf_reference.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16-bit RGB565 to match ESP32 firmware.
 * Widget from_json assigns RGB565 values directly to lv_color_t.full. */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Use LVGL built-in tick (not esp_timer) */
#define LV_TICK_CUSTOM 0

/* Use standard malloc/free */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 1
#define LV_MEM_CUSTOM_INCLUDE "stdlib.h"
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_MEM_SIZE (256 * 1024)
#define LV_MEMCPY_MEMSET_STD 1

/* Display refresh */
#define LV_DISP_DEF_REFR_PERIOD 14
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_DPI_DEF 130

/* Drawing */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 2

#define LV_IMG_CACHE_DEF_SIZE 16
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 256
#define LV_DITHER_GRADIENT 0

/* Logging */
#define LV_USE_LOG 0

/* Snapshot (not needed for preview) */
#define LV_USE_SNAPSHOT 0

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Label */
#define LV_USE_LABEL 1
#if LV_USE_LABEL
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT 1
#endif

/* Performance monitor (disabled) */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

/* ── Fonts: Montserrat 8-24 (match firmware) ── */
#define LV_FONT_MONTSERRAT_8  1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ── TinyTTF for dynamic font loading ── */
#define LV_USE_TINY_TTF 1
#if LV_USE_TINY_TTF
#define LV_TINY_TTF_FILE_SUPPORT 1
#endif

/* ── Custom font declarations (Fugaz, Manrope from firmware) ── */
/* These are resolved by font_manager via lv_tiny_ttf at runtime.
 * We declare them as externs so compiled references resolve.
 * If no TTF is loaded, widget_resolve_font() returns NULL and
 * the widget falls back to a Montserrat built-in. */
#define LV_FONT_CUSTOM_DECLARE \
    extern const lv_font_t ui_font_fugaz_14; \
    extern const lv_font_t ui_font_fugaz_17; \
    extern const lv_font_t ui_font_fugaz_28; \
    extern const lv_font_t ui_font_fugaz_56; \
    extern const lv_font_t ui_font_Manrope_35_BOLD; \
    extern const lv_font_t ui_font_Manrope_54_BOLD;

#endif /* LV_CONF_H */
