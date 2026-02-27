#pragma once
// ============================================================
// storage.h — Fiber Signer Key Storage
// LittleFS for encrypted key blob; NVS for attempt counter.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "crypto.h"

// ── Key blob layout (stored in LittleFS at /keys/master.bin) ──
// ciphertext format: [nonce:12][tag:16][ciphertext:192]  = 220 bytes total
// plaintext layout:
//   [  0: 32] master_seed               (32 bytes)
//   [ 32: 64] fiber_identity_key        m/fiber/0
//   [ 64: 96] fiber_funding_key         m/fiber/1
//   [ 96:128] fiber_revocation_key      m/fiber/2
//   [128:160] fiber_htlc_key            m/fiber/3
//   [160:192] fiber_payment_key         m/fiber/4
// Total plaintext: 192 bytes

#define KEY_BLOB_PLAINTEXT_SIZE  192   // 6 × 32
#define KEY_BLOB_NONCE_SIZE       12
#define KEY_BLOB_TAG_SIZE         16
#define KEY_BLOB_CIPHERTEXT_SIZE  KEY_BLOB_PLAINTEXT_SIZE
#define KEY_BLOB_TOTAL_SIZE      (KEY_BLOB_NONCE_SIZE + KEY_BLOB_TAG_SIZE + KEY_BLOB_CIPHERTEXT_SIZE)
// = 220 bytes

#define KEY_BLOB_PATH  "/keys/master.bin"
#define NVS_NAMESPACE  "fibersigner"
#define NVS_KEY_ATTEMPTS "pin_attempts"

// ── Initialisation ───────────────────────────────────────────
// Mounts LittleFS and initialises NVS. Call once on boot.
// Returns false if filesystem init fails.
bool storage_init(void);

// ── Key persistence ──────────────────────────────────────────
// Derive the wrapping key from PIN + efuse device ID via HKDF,
// then AES-256-GCM encrypt the KeyMaterial and write to flash.
bool storage_store_keys(const KeyMaterial *km, const char *pin);

// Load and decrypt the key blob from flash.
// Returns false if no key blob exists, or decryption fails (wrong PIN).
// On success, km is populated and must be zeroed by caller after use.
bool storage_load_keys(KeyMaterial *km, const char *pin);

// Securely erase the key blob and reset NVS attempt counter.
// Called after MAX_PIN_ATTEMPTS failures.
bool storage_wipe_keys(void);

// Check whether a key blob exists in flash.
bool storage_has_keys(void);

// ── PIN attempt counter (NVS) ────────────────────────────────
// Returns 0..MAX_PIN_ATTEMPTS; 0 means no failures recorded.
int  storage_get_attempts(void);

// Increment attempt counter. Returns new count.
int  storage_increment_attempts(void);

// Reset attempt counter to 0 (call after successful unlock).
void storage_reset_attempts(void);

// ── Wrapping key derivation ──────────────────────────────────
// Internal but exposed for testing:
// Derives 32-byte AES key = HKDF-SHA256(PIN || efuse_device_id, salt="fiber-signer-v1")
bool storage_derive_wrap_key(const char *pin, uint8_t wrap_key_out[32]);
