#pragma once
// ============================================================
// musig2.h — MuSig2 Two-Round Signing (MVP stub)
// Implements BIP-327 MuSig2 partial signing for CKB Fiber.
// Uses secp256k1 Schnorr primitives from trezor-crypto.
// ============================================================

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── MuSig2 Session State ────────────────────────────────────
// One session per channel open/commitment signing.
// Session is ephemeral: cleared after round2 completes.

#define MUSIG2_MAX_SESSIONS  4      // Max concurrent sessions
#define MUSIG2_SESSION_ID_LEN 32    // Session ID = 32 bytes

typedef struct {
    bool     active;
    char     session_id[MUSIG2_SESSION_ID_LEN * 2 + 1]; // hex string
    uint8_t  privkey[32];      // our private key for this session
    uint8_t  k1[32];           // secret nonce 1
    uint8_t  k2[32];           // secret nonce 2
    uint8_t  R1[33];           // public nonce 1 (compressed)
    uint8_t  R2[33];           // public nonce 2 (compressed)
    uint8_t  message[32];      // message being signed (from round1)
} MuSig2Session;

// ── Initialise session store ─────────────────────────────────
void musig2_init(void);

// ── Round 1 — Nonce Generation ───────────────────────────────
// session_id_hex: hex string identifier (matches round 2)
// privkey:        our 32-byte private key (not stored long-term)
// message:        the 32-byte message we expect to sign
// R1_hex_out:     33-byte compressed point, hex-encoded (67 chars + NUL)
// R2_hex_out:     33-byte compressed point, hex-encoded (67 chars + NUL)
// Returns false if session table is full or RNG fails.
bool musig2_round1(const char    *session_id_hex,
                   const uint8_t  privkey[32],
                   const uint8_t  message[32],
                   char           R1_hex_out[67],
                   char           R2_hex_out[67]);

// ── Round 2 — Partial Signature ─────────────────────────────
// session_id_hex:    matches the session from round 1
// agg_nonce_hex:     aggregated public nonce (66 bytes = 2×33 compressed, hex)
// counterparty_pub:  33-byte compressed pubkey of the other party (hex)
// partial_sig_out:   32-byte partial scalar (hex, 65 chars + NUL)
// Returns false if session not found or crypto fails.
bool musig2_round2(const char *session_id_hex,
                   const char *agg_nonce_hex,
                   const char *message_hex,
                   const char *counterparty_pub_hex,
                   char        partial_sig_out[65]);

// ── Session cleanup ──────────────────────────────────────────
// Clears a session after use (or on lock).
void musig2_clear_session(const char *session_id_hex);
void musig2_clear_all_sessions(void);

#ifdef __cplusplus
}
#endif
