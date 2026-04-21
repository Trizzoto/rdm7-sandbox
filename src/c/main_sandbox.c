/* main_sandbox.c — WASM entry point for the RDM-7 Dash sandbox.
 *
 * Boot sequence:
 *   1. hal_init()                — sets up LVGL + SDL2 display driver
 *   2. pointer_injector_init()    — registers the virtual indev that
 *                                    the JS side feeds via sandbox_set_pointer
 *   3. first_run_wizard_start()   — the firmware call that builds the
 *                                    wizard UI on the active screen
 *   4. requestAnimationFrame loop — calls sandbox_step at ~60 Hz which
 *                                    advances LVGL and ticks any mocks
 *                                    that need wall-clock updates.
 */
#include <emscripten.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_idf_shim.h"
#include "lvgl.h"

/* Hand-rolled wizard — src/c/wizard_sandbox.c */
extern void wizard_sandbox_start(void);

/* Pointer injector. */
extern void pointer_injector_init(void);
extern void pointer_injector_set(int16_t x, int16_t y, bool pressed);

extern void hal_init(void);
extern void font_stubs_init(void);

/* Firmware subsystems (widget + signal + font registries). */
extern void font_manager_init(void);
extern void signal_registry_init(void);
extern void signal_inject_test_value(const char *name, float v);

static double s_last_tick_ms = 0;

EMSCRIPTEN_KEEPALIVE
void sandbox_step(void) {
    double now = emscripten_get_now();
    uint32_t elapsed = (uint32_t)(now - s_last_tick_ms);
    if (elapsed < 1)   elapsed = 1;
    if (elapsed > 100) elapsed = 100;
    s_last_tick_ms = now;

    lv_tick_inc(elapsed);
    lv_task_handler();
}

/* Called from JS: sandbox_set_pointer(x, y, pressed)
 *   pressed = 1 for down/move, 0 for up. */
EMSCRIPTEN_KEEPALIVE
void sandbox_set_pointer(int x, int y, int pressed) {
    pointer_injector_set((int16_t)x, (int16_t)y, pressed != 0);
}

/* JS bridge: inject a synthetic signal value. Wraps the firmware's
 * signal_inject_test_value so host pages can drive the dashboard
 * from mock CAN data generated in JavaScript. */
EMSCRIPTEN_KEEPALIVE
void sandbox_inject_signal(const char *name, float value) {
    if (!name) return;
    signal_inject_test_value(name, value);
}

int main(void) {
    printf("[sandbox] main: initialising LVGL\n");
    lv_init();

    printf("[sandbox] main: initialising HAL\n");
    hal_init();

    /* MEMFS scaffolding so fopen() calls from font_manager / widgets
     * succeed. No actual files shipped yet — tours that need custom
     * fonts or images can register them via Module.FS at runtime. */
    mkdir("/lfs",           0755);
    mkdir("/lfs/fonts",     0755);
    mkdir("/lfs/images",    0755);
    mkdir("/lfs/layouts",   0755);

    /* Firmware subsystems — have to be live before any widget create. */
    font_stubs_init();
    font_manager_init();
    signal_registry_init();

    printf("[sandbox] main: registering virtual pointer indev\n");
    pointer_injector_init();

    printf("[sandbox] main: starting first-run wizard\n");
    wizard_sandbox_start();

    s_last_tick_ms = emscripten_get_now();
    /* Don't call emscripten_set_main_loop — we let the JS side drive
     * the tick via requestAnimationFrame + sandbox_step. That gives the
     * tutorial runner full control over pausing (e.g. during a long
     * voiceover line, we stop advancing). */
    printf("[sandbox] main: ready\n");
    return 0;
}
