/* esp_idf_shim.h — minimal ESP-IDF API shim for the sandbox WASM build.
 *
 * The firmware code assumes an ESP-IDF environment. In the browser we
 * replace the subsystems we don't need (NVS, FreeRTOS, CAN driver,
 * LedC, WiFi radio, OTA) with no-op / canned-data implementations.
 * Only the minimum surface touched by the Phase 1 wizard is here;
 * add more as linker errors surface when porting new screens.
 *
 * Include this BEFORE any other header in the sandbox C sources.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>

/* ── Logging ── */
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

/* ── Error codes ── */
typedef int esp_err_t;
#define ESP_OK                 0
#define ESP_FAIL              -1
#define ESP_ERR_NO_MEM         0x101
#define ESP_ERR_INVALID_ARG    0x102
#define ESP_ERR_INVALID_STATE  0x103
#define ESP_ERR_TIMEOUT        0x107
#define ESP_ERR_NOT_FOUND      0x105
#define ESP_ERR_NOT_SUPPORTED  0x106

static inline const char *esp_err_to_name(esp_err_t err) {
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    default: return "UNKNOWN";
    }
}

/* Macro the firmware uses that's absent in a hosted build. */
#define ESP_ERROR_CHECK(x) do { esp_err_t _e = (x); (void)_e; } while (0)

/* ── Memory ── */
#define MALLOC_CAP_SPIRAM   (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_32BIT    (1 << 4)

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps; return malloc(size);
}
static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps; return calloc(n, size);
}
/* Declared here; implemented in esp_idf_shim.c because aligned_alloc's
 * availability depends on toolchain (Emscripten supplies it; MSVC does
 * not). Not expected to be reached from the Phase 1 wizard — the
 * firmware sites that call it are all capture/display code we don't
 * port. Kept as a linker safety net. */
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
static inline void heap_caps_free(void *ptr) { free(ptr); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 8 * 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 2 * 1024 * 1024; }
static inline size_t esp_get_free_heap_size(void) { return 4 * 1024 * 1024; }
static inline size_t esp_get_minimum_free_heap_size(void) { return 2 * 1024 * 1024; }

/* ── Timers ── */
typedef void *esp_timer_handle_t;
typedef struct {
    void (*callback)(void*);
    const char *name;
    void *arg;
} esp_timer_create_args_t;
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) {
    (void)a; *h = NULL; return ESP_OK;
}
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) { (void)h; (void)us; return ESP_OK; }
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) { (void)h; (void)us; return ESP_OK; }
static inline esp_err_t esp_timer_stop(esp_timer_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t esp_timer_delete(esp_timer_handle_t h) { (void)h; return ESP_OK; }
int64_t  esp_timer_get_time(void);  /* impl in esp_idf_shim.c — uses emscripten_get_now */

/* ── NVS (no-op; real backing in stubs/config_store_mock.c) ── */
typedef uint32_t nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY  0
static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_erase(void) { return ESP_OK; }
static inline esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; (void)mode; *h = 0; return ESP_OK; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v)       { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v)     { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *v, size_t *len) { (void)h; (void)k; (void)v; (void)len; return ESP_ERR_NOT_FOUND; }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v)      { (void)h; (void)k; (void)v; return ESP_ERR_NOT_FOUND; }
static inline esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v)    { (void)h; (void)k; (void)v; return ESP_ERR_NOT_FOUND; }
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; return ESP_OK; }

/* ── FreeRTOS ── */
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY        0xFFFFFFFF
#define pdTRUE               1
#define pdFALSE              0
#define pdPASS               1
#define pdMS_TO_TICKS(ms)    (ms)
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)           { return (void*)1; }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)  { return (void*)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, uint32_t t)     { (void)s; (void)t; return pdTRUE; }
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s, uint32_t t) { (void)s; (void)t; return pdTRUE; }
static inline int xSemaphoreGive(SemaphoreHandle_t s)                 { (void)s; return pdTRUE; }
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t s)        { (void)s; return pdTRUE; }
static inline void vTaskDelay(TickType_t t)                           { (void)t; }
static inline TickType_t xTaskGetTickCount(void)                      { return 0; }

#define configASSERT(x) do { if(!(x)) { printf("ASSERT: %s\n", #x); abort(); } } while(0)

/* ── WiFi radio (not used — see stubs/wifi_mock.c for higher-level
 *  wifi_manager API). Keep these so firmware headers compile. ── */
typedef int wifi_auth_mode_t;

/* ── LittleFS ── */
typedef struct {
    const char *base_path;
    const char *partition_label;
    bool format_if_mount_failed;
    bool dont_mount;
} esp_vfs_littlefs_conf_t;
static inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c) { (void)c; return ESP_OK; }

/* ── Atomics (Emscripten workaround) ── */
#ifndef __cplusplus
  #ifdef _Atomic
    #undef _Atomic
  #endif
  #define _Atomic
#endif
