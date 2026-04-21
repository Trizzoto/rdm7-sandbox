/* screen_config.h — firmware-compatible screen geometry defines.
 * The firmware uses these to center-align widgets; for the sandbox we
 * hardcode 800×480. */
#pragma once

#define SCREEN_W   800
#define SCREEN_H   480
#define ORIGIN_X   (SCREEN_W / 2)
#define ORIGIN_Y   (SCREEN_H / 2)
