/*
 * state.h — global app state (node info, channels, signer status)
 * Updated by background poll thread, read by UI thread.
 * All writes under g_state_mutex.
 */
#pragma once

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#define FK_MAX_CHANNELS   64
#define FK_MAX_PEERS      32
#define FK_PUBKEY_LEN     67    /* "02..." hex string */
#define FK_ADDR_LEN       64
#define FK_TXID_LEN       67

typedef enum {
    CHAN_OPEN = 0,
    CHAN_PENDING,
    CHAN_CLOSING,
    CHAN_CLOSED,
} ChanState;

typedef struct {
    char      channel_id[FK_TXID_LEN];
    char      peer_id[FK_PUBKEY_LEN];
    char      peer_name[64];            /* from gossip, may be empty */
    int64_t   local_balance_ckb;        /* millionths of CKB (shannons) */
    int64_t   remote_balance_ckb;
    int64_t   capacity_ckb;
    ChanState state;
    bool      is_public;
} FkChannel;

typedef struct {
    /* Node */
    char     node_id[FK_PUBKEY_LEN];
    char     node_version[32];
    int      peer_count;
    int      channel_count;

    /* Channels */
    FkChannel channels[FK_MAX_CHANNELS];
    int       channel_len;

    /* Signer */
    bool     signer_connected;
    bool     signer_unlocked;
    char     signer_pubkey[FK_PUBKEY_LEN];
    char     signer_firmware[32];

    /* Status */
    bool     bridge_ok;
    char     last_error[256];
    uint64_t last_update_ms;

    /* Selected channel (for send flow) */
    int      selected_channel_idx;   /* -1 = none */

    /* Active invoice (for receive flow) */
    char     active_invoice[512];
    char     active_payment_hash[FK_TXID_LEN];
    int64_t  active_invoice_amount;
} FkState;

extern FkState        g_state;
extern pthread_mutex_t g_state_mutex;

void fk_state_init(void);

/* Lock-free snapshot for UI reads */
FkState fk_state_snapshot(void);
