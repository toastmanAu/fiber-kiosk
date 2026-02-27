/*
 * lv_conf.h — LVGL 9 config for fiber-kiosk
 * OPi 3B: 800×480 framebuffer, 16-bit colour (RGB565)
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH          16          /* RGB565 — standard for DSI panels */
#define LV_COLOR_16_SWAP        0           /* swap bytes? check with your panel */

#define LV_HOR_RES_MAX          800
#define LV_VER_RES_MAX          480

/* Memory */
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (512U * 1024U)   /* 512KB for LVGL heap */
#define LV_MEM_BUF_MAX_NUM      16

/* Rendering: double buffer, 1/10 screen height each */
#define LV_DRAW_BUF_ALIGN       4
#define LV_USE_DRAW_SW          1
#define LV_USE_DRAW_SW_ASM      LV_DRAW_SW_ASM_NONE

/* Tick — driven by our own timer in main.c */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "src/core/tick.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (lv_tick_get_ms())

/* Fonts */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_16

/* Widgets we use */
#define LV_USE_LABEL            1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_LIST             1
#define LV_USE_TABLE            1
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_IMG              1
#define LV_USE_QRCODE           1       /* built-in QR widget */
#define LV_USE_CANVAS           1
#define LV_USE_MSGBOX           1
#define LV_USE_SPINNER          1
#define LV_USE_KEYBOARD         0       /* using custom numpad */
#define LV_USE_TEXTAREA         1
#define LV_USE_TABVIEW          1

/* Display driver */
#define LV_USE_LINUX_FBDEV      1
#define LV_LINUX_FBDEV_BSD      0

/* Input driver */
#define LV_USE_EVDEV            1

/* Animation */
#define LV_USE_ANIMATION        1

/* Logging */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/* Perf monitor (disable in production) */
#define LV_USE_PERF_MONITOR     0

#endif /* LV_CONF_H */
