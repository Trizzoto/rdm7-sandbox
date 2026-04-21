/* screen_config.h — WASM stub for screen dimensions.
 * Default: 800x480 rectangle (matches the desktop editor canvas). */
#pragma once
#include <stdint.h>

typedef enum {
    SCREEN_SHAPE_RECT  = 0,
    SCREEN_SHAPE_ROUND = 1,
} screen_shape_t;

#define SCREEN_W          800
#define SCREEN_H          480
#define SCREEN_SHAPE      SCREEN_SHAPE_RECT
#define SCREEN_ORIGIN_X   (SCREEN_W / 2)
#define SCREEN_ORIGIN_Y   (SCREEN_H / 2)

typedef struct {
    uint16_t       width;
    uint16_t       height;
    uint16_t       origin_x;
    uint16_t       origin_y;
    screen_shape_t shape;
} screen_profile_t;

static inline const screen_profile_t *screen_get_profile(void) {
    static const screen_profile_t p = {
        SCREEN_W, SCREEN_H, SCREEN_ORIGIN_X, SCREEN_ORIGIN_Y, SCREEN_SHAPE
    };
    return &p;
}
