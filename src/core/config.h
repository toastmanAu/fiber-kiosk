/*
 * config.h — fiber-kiosk runtime config
 */
#pragma once

#include <stdint.h>

typedef struct {
    char   bridge_url[256];     /* http://127.0.0.1:7777 */
    char   signer_port[64];     /* /dev/ttyACM0 */
    char   fb_device[64];       /* /dev/fb0 */
    char   touch_device[64];    /* /dev/input/event0 */
    int    display_w;           /* 800 */
    int    display_h;           /* 480 */
    int    poll_ms;             /* 3000 — background refresh interval */
    int    pin_timeout_sec;     /* 300 — auto-lock after inactivity */
} FkConfig;

extern FkConfig g_config;

int  fk_config_load(const char *path);
void fk_config_defaults(void);
