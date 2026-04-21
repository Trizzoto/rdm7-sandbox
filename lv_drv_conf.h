/**
 * lv_drv_conf.h — lv_drivers configuration for SDL/Emscripten.
 */
#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

/* SDL display driver */
#define USE_SDL 1
#define SDL_HOR_RES 800
#define SDL_VER_RES 480
#define SDL_ZOOM 1
#define SDL_DOUBLE_BUFFERED 0
/* Emscripten's -sUSE_SDL=2 provides the header as <SDL.h> */
#define SDL_INCLUDE_PATH <SDL.h>

/* Disable unused drivers */
#define USE_FBDEV 0
#define USE_BSD_FBDEV 0
#define USE_DRM 0
#define USE_SUNXI_FBDEV 0
#define USE_MONITOR 0
#define USE_WINDOWS 0
#define USE_GTK 0
#define USE_WAYLAND 0
#define USE_EVDEV 0
#define USE_LIBINPUT 0
#define USE_XKB 0
#define USE_MOUSE 0
#define USE_MOUSEWHEEL 0
#define USE_KEYBOARD 0
#define USE_WIN32DRV 0
#define USE_SDL_GPU 0

#endif /* LV_DRV_CONF_H */
