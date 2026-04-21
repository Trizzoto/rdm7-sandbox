/* esp_idf_shim.c — non-inline shim implementations. */
#include "esp_idf_shim.h"
#include <emscripten.h>

int64_t esp_timer_get_time(void) {
    /* Return microseconds since module init, matching ESP-IDF semantics. */
    return (int64_t)(emscripten_get_now() * 1000.0);
}

/* Portability wrapper — Emscripten supports aligned_alloc (C11) via its
 * musl-based libc, but we keep this behind a function so non-emcc
 * compilers scanning the header (clangd on Windows, IDEs) don't error
 * on the missing identifier. */
#include <stdlib.h>
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)caps;
    /* aligned_alloc requires size to be a multiple of alignment. */
    if (size % alignment) size += alignment - (size % alignment);
    return aligned_alloc(alignment, size);
}
