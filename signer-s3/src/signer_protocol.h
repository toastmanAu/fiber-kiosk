#pragma once
// ============================================================
// signer_protocol.h — Fiber Signer JSON-RPC Protocol Definitions
// Shared between ESP32-S3 and ESP32-P4 signer variants.
// ============================================================

#include <stdint.h>

// ── JSON-RPC Standard Error Codes ───────────────────────────
#define JSONRPC_PARSE_ERROR       (-32700)
#define JSONRPC_INVALID_REQUEST   (-32600)
#define JSONRPC_METHOD_NOT_FOUND  (-32601)
#define JSONRPC_INVALID_PARAMS    (-32602)

// ── Signer-Specific Error Codes ─────────────────────────────
#define SIGNER_ERR_LOCKED          1   // Signer is locked; PIN required
#define SIGNER_ERR_BAD_PIN         2   // Wrong PIN supplied
#define SIGNER_ERR_WIPED           3   // Device wiped after too many failed attempts
#define SIGNER_ERR_CRYPTO          4   // Crypto operation failed
#define SIGNER_ERR_NO_KEYS         5   // No keys present; must generate first
#define SIGNER_ERR_INVALID_KEY     6   // key_index out of range
#define SIGNER_ERR_SESSION         7   // MuSig2 session not found

// ── Key Indices (m/fiber/N) ──────────────────────────────────
#define KEY_IDENTITY    0   // m/fiber/0 — Node identity / peer auth
#define KEY_FUNDING     1   // m/fiber/1 — Channel funding (MuSig2 participant)
#define KEY_REVOCATION  2   // m/fiber/2 — Revocation base key
#define KEY_HTLC        3   // m/fiber/3 — HTLC signing key
#define KEY_PAYMENT     4   // m/fiber/4 — Payment key
#define KEY_COUNT       5

// ── Protocol Constants ───────────────────────────────────────
#define MAX_FRAME_SIZE    4096
#define MAX_PIN_LEN       32
#define MAX_PIN_ATTEMPTS  5

// ── Response frame ───────────────────────────────────────────
// All responses are newline-terminated JSON:
//   {"jsonrpc":"2.0","id":<N>,"result":{...}}\n
//   {"jsonrpc":"2.0","id":<N>,"error":{"code":<C>,"message":"..."}}\n

// ── RPC Method Names ─────────────────────────────────────────
#define METHOD_UNLOCK         "unlock"
#define METHOD_LOCK           "lock"
#define METHOD_GET_STATUS     "get_status"
#define METHOD_GET_PUBKEY     "get_pubkey"
#define METHOD_SIGN_TX        "sign_tx"
#define METHOD_SIGN_HTLC      "sign_htlc"
#define METHOD_MUSIG2_ROUND1  "musig2_round1"
#define METHOD_MUSIG2_ROUND2  "musig2_round2"
#define METHOD_GENERATE_KEYS  "generate_keys"

// ── Firmware version ─────────────────────────────────────────
#define FIRMWARE_VERSION  "s3-signer-v0.1"
#define BOARD_NAME        "esp32-s3"
