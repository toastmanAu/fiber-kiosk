/*
 * state.c
 */
#include "state.h"
#include <string.h>

FkState         g_state;
pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

void fk_state_init(void) {
    pthread_mutex_lock(&g_state_mutex);
    memset(&g_state, 0, sizeof(g_state));
    g_state.selected_channel_idx = -1;
    g_state.bridge_ok            = false;
    g_state.signer_connected     = false;
    g_state.signer_unlocked      = false;
    pthread_mutex_unlock(&g_state_mutex);
}

FkState fk_state_snapshot(void) {
    FkState snap;
    pthread_mutex_lock(&g_state_mutex);
    snap = g_state;
    pthread_mutex_unlock(&g_state_mutex);
    return snap;
}
