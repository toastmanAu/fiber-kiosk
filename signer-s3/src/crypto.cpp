// ============================================================
// crypto.cpp — Fiber Signer Crypto Implementation
// ESP32-S3 variant using trezor-crypto + mbedTLS
// ============================================================

#include "crypto.h"
#include <string.h>
#include <stdlib.h>

// trezor-crypto
extern "C" {
#include "trezor_crypto/ecdsa.h"
#include "trezor_crypto/secp256k1.h"
#include "trezor_crypto/sha2.h"
#include "trezor_crypto/hmac.h"
#include "trezor_crypto/rand.h"
#include "trezor_crypto/memzero.h"
#include "blake2b/blake2.h"
}

// mbedTLS — built into ESP-IDF (AES-GCM, no HKDF in default sdkconfig)
#include "mbedtls/gcm.h"

// Arduino ESP32 hardware RNG
#include "esp_random.h"

// Note: rand.c already provides random32() and random_buffer() for ESP_PLATFORM
// using esp_random(). No need to redefine here.

// ── Init ─────────────────────────────────────────────────────
bool crypto_init(void) {
    // ESP32-S3 hardware RNG is always available.
    // mbedTLS uses the ESP hardware entropy source by default.
    return true;
}

// ── Key Generation ───────────────────────────────────────────
bool crypto_generate_keys(KeyMaterial *km) {
    if (!km) return false;
    // Fill master seed with hardware entropy
    esp_fill_random(km->master_seed, 32);
    // Derive all fiber keys
    return crypto_derive_all_keys(km);
}

// ── HMAC-SHA512 key derivation ───────────────────────────────
// Derive fiber key at index from master seed:
//   HMAC-SHA512(key=master_seed, data="fiber" || uint8(index))
// We take the first 32 bytes of the 64-byte HMAC output as the key.
bool crypto_derive_fiber_key(KeyMaterial *km, uint8_t index) {
    if (!km || index >= KEY_COUNT) return false;

    // Label: "fiber" + index byte
    uint8_t label[6];
    memcpy(label, "fiber", 5);
    label[5] = index;

    uint8_t hmac_out[64];
    HMAC_SHA512_CTX ctx;
    ubtc_hmac_sha512_Init(&ctx, km->master_seed, 32);
    ubtc_hmac_sha512_Update(&ctx, label, sizeof(label));
    ubtc_hmac_sha512_Final(&ctx, hmac_out);

    // The first 32 bytes become the private key.
    // secp256k1 requires privkey < curve order; retry if invalid (astronomically rare).
    const ecdsa_curve *curve = &secp256k1;
    bool valid = false;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (attempt == 0) {
            memcpy(km->keys[index], hmac_out, 32);
        } else {
            // Rehash: HMAC-SHA256(seed, hmac_out)
            uint8_t retry_out[32];
            ubtc_hmac_sha256(km->master_seed, 32, hmac_out, 32, retry_out);
            memcpy(km->keys[index], retry_out, 32);
        }
        // Validate: key != 0 and key < curve order (bignum check)
        bignum256 k;
        bn_read_be(km->keys[index], &k);
        if (!bn_is_zero(&k) && bn_is_less(&k, &curve->order)) {
            valid = true;
            break;
        }
    }

    crypto_memzero(hmac_out, sizeof(hmac_out));
    return valid;
}

bool crypto_derive_all_keys(KeyMaterial *km) {
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (!crypto_derive_fiber_key(km, i)) return false;
    }
    return true;
}

// ── Public Key ───────────────────────────────────────────────
bool crypto_pubkey(const uint8_t privkey[32], uint8_t pubkey_out[33]) {
    if (!privkey || !pubkey_out) return false;
    // ecdsa_get_public_key33 uses secp256k1 curve
    ecdsa_get_public_key33(&secp256k1, privkey, pubkey_out);
    return true;
}

// ── Signing ──────────────────────────────────────────────────
bool crypto_sign(const uint8_t privkey[32],
                 const uint8_t hash[32],
                 uint8_t sig_out[64]) {
    if (!privkey || !hash || !sig_out) return false;

    uint8_t sig_der[72];   // DER buffer (trezor ecdsa_sign_digest)
    uint8_t pby;           // recovery id

    // ecdsa_sign_digest: signs a pre-hashed 32-byte digest
    // Uses RFC6979 deterministic nonce (USE_RFC6979=1)
    int ret = ecdsa_sign_digest(&secp256k1,
                                privkey,
                                hash,
                                sig_der,   // 64-byte compact output (r||s)
                                &pby,
                                NULL);     // no additional check fn

    if (ret != 0) return false;

    // trezor ecdsa_sign_digest writes compact 64-byte sig directly into first param
    // when last arg is NULL — sig_der actually contains 64 bytes r||s
    memcpy(sig_out, sig_der, 64);
    return true;
}

// ── Blake2b ──────────────────────────────────────────────────
bool crypto_blake2b(const uint8_t *in, size_t inlen, uint8_t out[32]) {
    if (!in || !out) return false;
    blake2b_state S;
    if (blake2b_init(&S, 32) < 0) return false;
    blake2b_update(&S, in, inlen);
    blake2b_final(&S, out, 32);
    return true;
}

// ── HKDF-SHA256 ──────────────────────────────────────────────
// Manual implementation using trezor-crypto HMAC-SHA256.
// RFC 5869: Extract + Expand
bool crypto_hkdf_sha256(const uint8_t *ikm,  size_t ikm_len,
                        const uint8_t *salt, size_t salt_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *okm, size_t okm_len) {
    if (!ikm || !okm || okm_len == 0 || okm_len > 32 * 255) return false;

    // Default salt is a string of HashLen zeros
    static const uint8_t default_salt[32] = {0};
    if (!salt || salt_len == 0) {
        salt = default_salt;
        salt_len = 32;
    }

    // Extract: PRK = HMAC-SHA256(salt, IKM)
    uint8_t prk[32];
    ubtc_hmac_sha256(salt, (uint32_t)salt_len,
                     ikm,  (uint32_t)ikm_len,
                     prk);

    // Expand: T(1) = HMAC-SHA256(PRK, "" || info || 0x01)
    //         T(i) = HMAC-SHA256(PRK, T(i-1) || info || i)
    // For our use case okm_len <= 32, so one block suffices.
    uint8_t T[32] = {0};
    uint8_t counter = 0;
    size_t done = 0;

    while (done < okm_len) {
        counter++;
        // Build input: T_prev || info || counter
        size_t prev_len = (counter == 1) ? 0 : 32;
        size_t input_len = prev_len + info_len + 1;
        uint8_t *input = (uint8_t *)malloc(input_len);
        if (!input) return false;

        if (prev_len > 0) memcpy(input, T, prev_len);
        if (info && info_len > 0) memcpy(input + prev_len, info, info_len);
        input[prev_len + info_len] = counter;

        ubtc_hmac_sha256(prk, 32, input, (uint32_t)input_len, T);
        free(input);

        size_t copy = okm_len - done;
        if (copy > 32) copy = 32;
        memcpy(okm + done, T, copy);
        done += copy;
    }

    crypto_memzero(prk, sizeof(prk));
    crypto_memzero(T, sizeof(T));
    return true;
}

// ── AES-256-GCM Encrypt ──────────────────────────────────────
bool crypto_aes256gcm_encrypt(const uint8_t key[32],
                              const uint8_t *plaintext, size_t pt_len,
                              uint8_t *ciphertext,
                              uint8_t nonce_out[12],
                              uint8_t tag_out[16]) {
    if (!key || !plaintext || !ciphertext || !nonce_out || !tag_out) return false;

    // Generate random 12-byte nonce using hardware RNG
    esp_fill_random(nonce_out, 12);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    ret = mbedtls_gcm_crypt_and_tag(&ctx,
                                    MBEDTLS_GCM_ENCRYPT,
                                    pt_len,
                                    nonce_out, 12,
                                    NULL, 0,          // no AAD
                                    plaintext,
                                    ciphertext,
                                    16, tag_out);

    mbedtls_gcm_free(&ctx);
    return (ret == 0);
}

// ── AES-256-GCM Decrypt ──────────────────────────────────────
bool crypto_aes256gcm_decrypt(const uint8_t key[32],
                              const uint8_t nonce[12],
                              const uint8_t tag[16],
                              const uint8_t *ciphertext, size_t ct_len,
                              uint8_t *plaintext) {
    if (!key || !nonce || !tag || !ciphertext || !plaintext) return false;

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    ret = mbedtls_gcm_auth_decrypt(&ctx,
                                   ct_len,
                                   nonce, 12,
                                   NULL, 0,          // no AAD
                                   tag, 16,
                                   ciphertext,
                                   plaintext);

    mbedtls_gcm_free(&ctx);
    return (ret == 0);
}

// ── Secure memzero ───────────────────────────────────────────
void crypto_memzero(void *buf, size_t len) {
    memzero(buf, len);   // trezor-crypto's volatile memzero
}

// ── Hex helpers ──────────────────────────────────────────────
void bytes_to_hex(const uint8_t *in, size_t len, char *hex_out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex_out[i * 2]     = hex_chars[in[i] >> 4];
        hex_out[i * 2 + 1] = hex_chars[in[i] & 0x0f];
    }
    hex_out[len * 2] = '\0';
}

int hex_to_bytes(const char *hex, uint8_t *out, size_t max_len) {
    if (!hex || !out) return -1;
    // Skip optional 0x prefix
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
    }
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t byte_len = hex_len / 2;
    if (byte_len > max_len) return -1;

    for (size_t i = 0; i < byte_len; i++) {
        int hi = -1, lo = -1;
        char c = hex[i * 2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return -1;

        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else return -1;

        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)byte_len;
}
