/**
 * font_stubs.c — Now a no-op since compiled fonts are linked directly.
 * The actual font data comes from fonts/ui_font_*.c files.
 */
#include "lvgl.h"

void font_stubs_init(void) {
    /* No-op: compiled font .c files provide the real font data */
}
