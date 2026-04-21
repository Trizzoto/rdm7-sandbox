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

# ── Sandbox sources ───────────────────────────────────────────────────
# The firmware's first_run_wizard.c pulls in 20+ headers (ESP-IDF driver
# types, widget registry, web server, CAN manager, NVS...) that would
# each need stubs to compile. Instead we hand-roll a visually-matched
# wizard in pure LVGL, with zero firmware coupling. Fast to build, fast
# to change, and the tour script drives it identically either way.
set(SANDBOX_SOURCES
    src/c/main_sandbox.c
    src/c/hal_init.c
    src/c/pointer_injector.c
    src/c/wizard_sandbox.c
    stubs/esp_idf_shim.c
)

add_executable(rdm7-sandbox ${SANDBOX_SOURCES})

target_include_directories(rdm7-sandbox PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/src/c
)

target_link_libraries(rdm7-sandbox lvgl lvgl_drivers)

set_target_properties(rdm7-sandbox PROPERTIES SUFFIX ".js")

target_compile_options(rdm7-sandbox PRIVATE
    -O2
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-unused-variable
)

target_link_options(rdm7-sandbox PRIVATE
    -sUSE_SDL=2
    -sINITIAL_MEMORY=33554432
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576
    -sNO_EXIT_RUNTIME=1
    -sMODULARIZE=1
    -sEXPORT_NAME='RDM7Sandbox'
    -sASSERTIONS=1
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_sandbox_set_pointer','_sandbox_step','_sandbox_set_scene','_malloc','_free']"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
)
