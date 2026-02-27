/*
 * screens.h — screen IDs and navigation
 */
#pragma once

typedef enum {
    SCREEN_PIN = 0,     /* PIN lock — shown first if signer connected */
    SCREEN_HOME,
    SCREEN_CHANNELS,
    SCREEN_SEND,
    SCREEN_CONFIRM,
    SCREEN_RECEIVE,
    SCREEN_SIGNER,
    SCREEN_COUNT
} FkScreen;

/* Navigate to screen (called from any screen's event handler) */
void fk_nav_to(FkScreen screen);

/* Called by main loop after poll thread updates state */
void fk_screens_refresh(void);

/* Build all screens once at startup */
void fk_screens_init(void);
