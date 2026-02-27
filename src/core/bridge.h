/*
 * bridge.h — HTTP client for fiber-bridge (local Node.js API)
 *
 * fiber-bridge wraps:
 *   - Fiber node RPC (fnn JSON-RPC via Biscuit auth)
 *   - ESP32-P4 signer UART bridge
 *
 * All calls are synchronous (called from poll thread, not UI thread).
 */
#pragma once

#include "state.h"
#include <stdbool.h>
#include <stdint.h>

/* Initialise libcurl (call once at startup) */
void fk_bridge_init(const char *base_url);

/* Poll node info + channels — fills g_state under mutex */
int fk_bridge_poll(void);

/* Channel operations */
int fk_bridge_open_channel(const char *peer_id, int64_t capacity_shannons, char *err, int errlen);
int fk_bridge_close_channel(const char *channel_id, char *err, int errlen);

/* Payment */
int fk_bridge_send_payment(
    const char *invoice,
    int64_t     max_fee_shannons,
    char       *payment_hash_out,   /* 67 bytes */
    char       *err, int errlen
);

/* Invoice */
int fk_bridge_new_invoice(
    int64_t     amount_shannons,
    const char *description,
    int         expiry_sec,
    char       *invoice_out,       /* 512 bytes */
    char       *payment_hash_out,  /* 67 bytes */
    char       *err, int errlen
);

/* Check invoice status — returns: 0=pending, 1=paid, -1=error/expired */
int fk_bridge_check_invoice(const char *payment_hash);

/* Signer */
int fk_bridge_signer_status(bool *connected, bool *unlocked, char *pubkey, char *fw);
int fk_bridge_signer_unlock(const char *pin, char *err, int errlen);
int fk_bridge_signer_lock(void);
