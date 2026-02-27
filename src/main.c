/*
 * main.c — fiber-kiosk entry point
 *
 * Init sequence:
 *   1. Load config
 *   2. Init LVGL + framebuffer + evdev touch
 *   3. Init bridge HTTP client
 *   4. Build all screens
 *   5. Show HOME
 *   6. Spawn poll thread (background state refresh)
 *   7. Run LVGL tick loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/display/lv_linux_fbdev.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"

#include "src/core/config.h"
#include "src/core/state.h"
#include "src/core/bridge.h"
#include "src/screens/screens.h"

/* ── Tick helper ────────────────────────────────────────────────── */
uint32_t lv_tick_get_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── Current screen ─────────────────────────────────────────────── */
static FkScreen  s_current_screen = SCREEN_HOME;
static lv_obj_t *s_screens[SCREEN_COUNT];

void fk_nav_to(FkScreen screen) {
    if (screen < 0 || screen >= SCREEN_COUNT) return;
    s_current_screen = screen;
    lv_scr_load_anim(s_screens[screen], LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

void fk_screens_refresh(void) {
    FkState snap = fk_state_snapshot();
    /* Refresh all screens' data labels (only visible one renders, but cheap) */
    extern void home_refresh(const FkState *);
    extern void channels_refresh(const FkState *);
    extern void send_refresh(const FkState *);
    extern void receive_refresh(const FkState *);
    home_refresh(&snap);
    channels_refresh(&snap);
    send_refresh(&snap);
    receive_refresh(&snap);
}

/* ── Background poll thread ─────────────────────────────────────── */
static volatile int s_running = 1;

static void *poll_thread(void *arg) {
    (void)arg;
    while (s_running) {
        fk_bridge_poll();
        /* Signal main loop to refresh UI */
        /* (LVGL is not thread-safe — we use a flag + refresh in main loop) */
        struct timespec ts = { .tv_sec = g_config.poll_ms / 1000,
                               .tv_nsec = (g_config.poll_ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void on_sigint(int sig) { (void)sig; s_running = 0; }

/* ── Build all screens ──────────────────────────────────────────── */
void fk_screens_init(void) {
    extern lv_obj_t *home_build(void);
    extern lv_obj_t *channels_build(void);
    extern lv_obj_t *send_build(void);
    extern lv_obj_t *confirm_build(void);
    extern lv_obj_t *receive_build(void);
    extern lv_obj_t *signer_screen_build(void);

    s_screens[SCREEN_HOME]     = home_build();
    s_screens[SCREEN_CHANNELS] = channels_build();
    s_screens[SCREEN_SEND]     = send_build();
    s_screens[SCREEN_CONFIRM]  = confirm_build();
    s_screens[SCREEN_RECEIVE]  = receive_build();
    s_screens[SCREEN_SIGNER]   = signer_screen_build();
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    /* Config */
    const char *conf_path = "/etc/fiber-kiosk.conf";
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--config")) conf_path = argv[i+1];
    }
    fk_config_load(conf_path);   /* falls back to defaults on missing file */

    /* Override from CLI args */
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--fb"))    strncpy(g_config.fb_device,    argv[i+1], 63);
        if (!strcmp(argv[i], "--touch")) strncpy(g_config.touch_device, argv[i+1], 63);
        if (!strcmp(argv[i], "--bridge"))strncpy(g_config.bridge_url,   argv[i+1], 255);
    }

    /* Bridge */
    fk_bridge_init(g_config.bridge_url);

    /* State */
    fk_state_init();

    /* LVGL */
    lv_init();

    /* Framebuffer display */
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, g_config.fb_device);

    /* Draw buffers — two buffers, 1/10 screen height each */
    static uint16_t buf1[800 * 48];
    static uint16_t buf2[800 * 48];
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_resolution(disp, g_config.display_w, g_config.display_h);

    /* Touch input */
    lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, g_config.touch_device);
    (void)indev;

    /* Build screens */
    fk_screens_init();

    /* Initial poll */
    fk_bridge_poll();
    fk_screens_refresh();

    /* Show home */
    lv_scr_load(s_screens[SCREEN_HOME]);

    /* Poll thread */
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    pthread_t ptid;
    pthread_create(&ptid, NULL, poll_thread, NULL);

    /* Main loop */
    uint32_t last_refresh = 0;
    while (s_running) {
        lv_task_handler();

        /* Refresh UI every poll interval */
        uint32_t now = lv_tick_get_ms();
        if (now - last_refresh > (uint32_t)g_config.poll_ms) {
            fk_screens_refresh();
            last_refresh = now;
        }

        usleep(5000); /* ~200 Hz loop */
    }

    /* Cleanup */
    pthread_join(ptid, NULL);
    lv_deinit();
    printf("fiber-kiosk: exited cleanly\n");
    return 0;
}
