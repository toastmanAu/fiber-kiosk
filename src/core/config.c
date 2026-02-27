/*
 * config.c — load /etc/fiber-kiosk.conf (INI-style, no deps)
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FkConfig g_config;

void fk_config_defaults(void) {
    strncpy(g_config.bridge_url,   "http://127.0.0.1:7777", sizeof(g_config.bridge_url)-1);
    strncpy(g_config.signer_port,  "/dev/ttyACM0",           sizeof(g_config.signer_port)-1);
    strncpy(g_config.fb_device,    "/dev/fb0",               sizeof(g_config.fb_device)-1);
    strncpy(g_config.touch_device, "/dev/input/event0",      sizeof(g_config.touch_device)-1);
    g_config.display_w       = 800;
    g_config.display_h       = 480;
    g_config.poll_ms         = 3000;
    g_config.pin_timeout_sec = 300;
}

int fk_config_load(const char *path) {
    fk_config_defaults();
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* strip comments and whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\n') continue;

        char key[128], val[256];
        if (sscanf(p, " %127[^= ] = %255[^\n]", key, val) != 2) continue;

        /* trim trailing whitespace from val */
        int vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\r' || val[vlen-1] == '\n'))
            val[--vlen] = '\0';

        if      (!strcmp(key, "bridge_url"))      strncpy(g_config.bridge_url,   val, sizeof(g_config.bridge_url)-1);
        else if (!strcmp(key, "signer_port"))     strncpy(g_config.signer_port,  val, sizeof(g_config.signer_port)-1);
        else if (!strcmp(key, "fb_device"))       strncpy(g_config.fb_device,    val, sizeof(g_config.fb_device)-1);
        else if (!strcmp(key, "touch_device"))    strncpy(g_config.touch_device, val, sizeof(g_config.touch_device)-1);
        else if (!strcmp(key, "display_w"))       g_config.display_w       = atoi(val);
        else if (!strcmp(key, "display_h"))       g_config.display_h       = atoi(val);
        else if (!strcmp(key, "poll_ms"))         g_config.poll_ms         = atoi(val);
        else if (!strcmp(key, "pin_timeout_sec")) g_config.pin_timeout_sec = atoi(val);
    }
    fclose(f);
    return 0;
}
