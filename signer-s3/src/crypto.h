#pragma once
// ============================================================
// crypto.h — Fiber Signer Crypto Operations
// secp256k1 signing, Blake2b, HKDF-SHA256, AES-256-GCM
// Uses trezor-crypto + mbedTLS (built into ESP-IDF)
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "signer_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Key material ─────────────────────────────────────────────
// 32 bytes each; KEY_COUNT keys total
typedef struct {
    uint8_t master_seed[32];
    uint8_t keys[KEY_COUNT][32];   // indexed by KEY_IDENTITY..KEY_PAYMENT
} KeyMaterial;

// ── Signature output (compact 64-byte + 1-byte recovery) ────
typedef struct {
    uint8_t r[32];
    uint8_t s[32];
    uint8_t v;      // recovery id (0 or 1)
} CompactSig;

// ── Initialisation ───────────────────────────────────────────
// Must be called once before any crypto operation.
bool crypto_init(void);

// ── Key Generation ───────────────────────────────────────────
// Generate fresh random master_seed, then derive all fiber keys.
// Fills km; returns false on RNG failure.
bool crypto_generate_keys(KeyMaterial *km);

// ── Key Derivation ───────────────────────────────────────────
// Derive fiber key N from master seed using HMAC-SHA512:
//   HMAC-SHA512(key=master_seed, msg="fiber" || uint32_be(index))
// Stores 32-byte derived private key in km->keys[index].
bool crypto_derive_fiber_key(KeyMaterial *km, uint8_t index);

// ── Derive all keys from master_seed ─────────────────────────
bool crypto_derive_all_keys(KeyMaterial *km);

// ── Public Key ───────────────────────────────────────────────
// Compute compressed 33-byte secp256k1 pubkey from privkey.
bool crypto_pubkey(const uint8_t privkey[32], uint8_t pubkey_out[33]);

// ── Signing ──────────────────────────────────────────────────
// Sign a 32-byte hash with a private key (secp256k1, RFC6979 deterministic).
// sig_out: 64 bytes [r:32][s:32]
// Returns false on failure.
bool crypto_sign(const uint8_t privkey[32],
                 const uint8_t hash[32],
                 uint8_t sig_out[64]);

// ── Blake2b ──────────────────────────────────────────────────
// Hash 'inlen' bytes to 32-byte Blake2b digest.
bool crypto_blake2b(const uint8_t *in, size_t inlen, uint8_t out[32]);

// ── HKDF-SHA256 ──────────────────────────────────────────────
// Derive 'okm_len' bytes of key material.
// ikm   = input keying material
// salt  = optional salt (may be NULL)
// info  = context label (may be NULL)
bool crypto_hkdf_sha256(const uint8_t *ikm,  size_t ikm_len,
                        const uint8_t *salt, size_t salt_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *okm, size_t okm_len);

// ── AES-256-GCM ──────────────────────────────────────────────
// Encrypt plaintext. nonce_out (12 bytes) and tag_out (16 bytes) written.
// Returns false on mbedTLS error.
bool crypto_aes256gcm_encrypt(const uint8_t key[32],
                              const uint8_t *plaintext, size_t pt_len,
                              uint8_t *ciphertext,
                              uint8_t nonce_out[12],
                              uint8_t tag_out[16]);

// Decrypt and authenticate. Returns false if tag check fails.
bool crypto_aes256gcm_decrypt(const uint8_t key[32],
                              const uint8_t nonce[12],
                              const uint8_t tag[16],
                              const uint8_t *ciphertext, size_t ct_len,
                              uint8_t *plaintext);

// ── Secure memzero ───────────────────────────────────────────
void crypto_memzero(void *buf, size_t len);

// ── Hex helpers ──────────────────────────────────────────────
// bytes_to_hex: writes 2*len+1 bytes (null-terminated) to hex_out.
void bytes_to_hex(const uint8_t *in, size_t len, char *hex_out);

// hex_to_bytes: parse hex string (with or without 0x prefix).
// Returns number of bytes written, or -1 on parse error.
int hex_to_bytes(const char *hex, uint8_t *out, size_t max_len);

#ifdef __cplusplus
}
#endif
