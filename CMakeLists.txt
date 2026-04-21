cmake_minimum_required(VERSION 3.10)
project(rdm7_sandbox C)

# ── Global emcc flags ──────────────────────────────────────────────────
# -sUSE_SDL=2 must be on every compile as well as the final link, because
# lv_drivers/sdl/sdl.c includes <SDL.h> at compile time.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -sUSE_SDL=2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sUSE_SDL=2")

# ── LVGL config ───────────────────────────────────────────────────────
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

# ── Firmware widget system (copied wholesale from rdm7-wasm-editor,
# which already adapts these sources for the WASM environment). This
# gives us 1-to-1 rendering of any layout JSON — the pixels coming
# out of the canvas are exactly what the device would draw. */
set(WIDGET_SOURCES
    widgets/widget_panel.c
    widgets/widget_bar.c
    widgets/widget_text.c
    widgets/widget_button.c
    widgets/widget_toggle.c
    widgets/widget_arc.c
    widgets/widget_meter.c
    widgets/widget_image.c
    widgets/widget_warning.c
    widgets/widget_shift_light.c
    widgets/widget_rpm_bar.c
    widgets/widget_indicator.c
    widgets/widget_shape_panel.c
    widgets/widget_types.c
    widgets/widget_registry.c
    widgets/widget_rules.c
    widgets/signal.c
    widgets/signal_internal.c
    widgets/font_manager.c
    can/can_decode.c
)

set(LAYOUT_SOURCES
    layout/layout_manager.c
)

set(FONT_SOURCES
    fonts/ui_font_fugaz_14.c
    fonts/ui_font_fugaz_17.c
    fonts/ui_font_fugaz_28.c
    fonts/ui_font_fugaz_56.c
    fonts/ui_font_Manrope_35_BOLD.c
    fonts/ui_font_Manrope_54_BOLD.c
)

set(IMAGE_SOURCES
    ui/images/ui_img_indicator_left_png.c
    ui/images/ui_img_indicator_right_png.c
)

# ── Sandbox stubs: replace ESP-IDF surface with no-ops ───────────────
set(STUB_SOURCES
    stubs/esp_idf_shim.c
    stubs/cJSON.c
    stubs/font_stubs.c
    stubs/ui_stubs.c
)

# ── Sandbox entry + tour wizard ──────────────────────────────────────
set(SANDBOX_SOURCES
    src/c/main_sandbox.c
    src/c/hal_init.c
    src/c/pointer_injector.c
    src/c/wizard_sandbox.c
    src/c/default_layout_mock.c
)

add_executable(rdm7-sandbox
    ${SANDBOX_SOURCES}
    ${WIDGET_SOURCES}
    ${LAYOUT_SOURCES}
    ${FONT_SOURCES}
    ${IMAGE_SOURCES}
    ${STUB_SOURCES}
)

target_include_directories(rdm7-sandbox PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/stubs/shims
    ${CMAKE_SOURCE_DIR}/src/c
    ${CMAKE_SOURCE_DIR}/widgets
    ${CMAKE_SOURCE_DIR}/layout
    ${CMAKE_SOURCE_DIR}/can
    ${CMAKE_SOURCE_DIR}/fonts
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/ui/screens
    ${CMAKE_SOURCE_DIR}/ui/settings
    ${CMAKE_SOURCE_DIR}/ui/images
)

target_link_libraries(rdm7-sandbox lvgl lvgl_drivers)

set_target_properties(rdm7-sandbox PROPERTIES SUFFIX ".js")

target_compile_options(rdm7-sandbox PRIVATE
    -O2
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)

target_link_options(rdm7-sandbox PRIVATE
    -sUSE_SDL=2
    -sINITIAL_MEMORY=67108864   # 64 MB — widgets + fonts + layouts
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576
    -sNO_EXIT_RUNTIME=1
    -sMODULARIZE=1
    -sEXPORT_NAME='RDM7Sandbox'
    -sASSERTIONS=1
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_sandbox_set_pointer','_sandbox_step','_sandbox_set_scene','_sandbox_inject_signal','_malloc','_free']"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','HEAPU8']"
)
