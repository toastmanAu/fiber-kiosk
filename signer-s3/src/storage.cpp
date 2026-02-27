// ============================================================
// storage.cpp — Fiber Signer Key Storage Implementation
// LittleFS for encrypted blob; NVS for tamper-evident counter.
// Security-critical: PIN attempt lockout + wipe enforced here.
// ============================================================

#include "storage.h"
#include "signer_protocol.h"
#include <string.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_efuse.h"
#include "esp_efuse_table.h"

// Debug serial (USB CDC)
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL Serial
#endif

// ── Efuse device ID ──────────────────────────────────────────
// ESP32-S3 has a unique 8-byte MAC / chip ID in efuse.
// We read it as a hardware secret bound to this chip.
static void get_efuse_device_id(uint8_t out[8]) {
    // Read the MAC address from eFuse (guaranteed unique per chip)
    esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, out, 48);  // 48 bits = 6 bytes
    out[6] = 0xF1;  // pad to 8 bytes with fixed constants
    out[7] = 0xBE;
}

// ── Wrapping key derivation ──────────────────────────────────
// wrap_key = HKDF-SHA256(IKM = PIN || efuse_id, salt="fiber-signer-v1", info="wrap-key")
bool storage_derive_wrap_key(const char *pin, uint8_t wrap_key_out[32]) {
    if (!pin || !wrap_key_out) return false;

    size_t pin_len = strlen(pin);
    uint8_t device_id[8];
    get_efuse_device_id(device_id);

    // IKM = PIN bytes || device_id (concatenated)
    size_t ikm_len = pin_len + sizeof(device_id);
    uint8_t *ikm = (uint8_t *)malloc(ikm_len);
    if (!ikm) return false;
    memcpy(ikm, pin, pin_len);
    memcpy(ikm + pin_len, device_id, sizeof(device_id));

    static const uint8_t salt[] = "fiber-signer-v1";
    static const uint8_t info[] = "wrap-key";

    bool ok = crypto_hkdf_sha256(ikm, ikm_len,
                                  salt, sizeof(salt) - 1,
                                  info, sizeof(info) - 1,
                                  wrap_key_out, 32);

    crypto_memzero(ikm, ikm_len);
    free(ikm);
    crypto_memzero(device_id, sizeof(device_id));
    return ok;
}

// ── LittleFS init ────────────────────────────────────────────
bool storage_init(void) {
    // NVS init (required for esp_efuse and nvs_open)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated; erase and reinit
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        DEBUG_SERIAL.printf("[storage] NVS init failed: %d\n", err);
        return false;
    }

    // Mount LittleFS
    if (!LittleFS.begin(true /* format on fail */)) {
        DEBUG_SERIAL.println("[storage] LittleFS mount failed");
        return false;
    }

    // Ensure /keys directory exists
    if (!LittleFS.exists("/keys")) {
        LittleFS.mkdir("/keys");
    }

    DEBUG_SERIAL.println("[storage] LittleFS + NVS ready");
    return true;
}

// ── Store keys ───────────────────────────────────────────────
bool storage_store_keys(const KeyMaterial *km, const char *pin) {
    if (!km || !pin) return false;

    // Build plaintext blob: [master_seed:32][keys[0..4]:5*32] = 192 bytes
    uint8_t plaintext[KEY_BLOB_PLAINTEXT_SIZE];
    memcpy(plaintext, km->master_seed, 32);
    for (int i = 0; i < KEY_COUNT; i++) {
        memcpy(plaintext + 32 + i * 32, km->keys[i], 32);
    }

    // Derive wrapping key
    uint8_t wrap_key[32];
    if (!storage_derive_wrap_key(pin, wrap_key)) {
        crypto_memzero(plaintext, sizeof(plaintext));
        return false;
    }

    // Encrypt
    uint8_t nonce[KEY_BLOB_NONCE_SIZE];
    uint8_t tag[KEY_BLOB_TAG_SIZE];
    uint8_t ciphertext[KEY_BLOB_CIPHERTEXT_SIZE];

    bool ok = crypto_aes256gcm_encrypt(wrap_key,
                                       plaintext, KEY_BLOB_PLAINTEXT_SIZE,
                                       ciphertext, nonce, tag);

    crypto_memzero(plaintext, sizeof(plaintext));
    crypto_memzero(wrap_key, sizeof(wrap_key));

    if (!ok) {
        DEBUG_SERIAL.println("[storage] AES-GCM encrypt failed");
        return false;
    }

    // Write blob: [nonce:12][tag:16][ciphertext:192]
    File f = LittleFS.open(KEY_BLOB_PATH, "w");
    if (!f) {
        DEBUG_SERIAL.println("[storage] Cannot open key blob for write");
        return false;
    }

    size_t n = 0;
    n += f.write(nonce,      KEY_BLOB_NONCE_SIZE);
    n += f.write(tag,        KEY_BLOB_TAG_SIZE);
    n += f.write(ciphertext, KEY_BLOB_CIPHERTEXT_SIZE);
    f.close();

    if (n != KEY_BLOB_TOTAL_SIZE) {
        DEBUG_SERIAL.printf("[storage] Wrote %u bytes, expected %u\n", n, KEY_BLOB_TOTAL_SIZE);
        LittleFS.remove(KEY_BLOB_PATH);
        return false;
    }

    DEBUG_SERIAL.println("[storage] Key blob written OK");
    return true;
}

// ── Load keys ────────────────────────────────────────────────
bool storage_load_keys(KeyMaterial *km, const char *pin) {
    if (!km || !pin) return false;

    if (!LittleFS.exists(KEY_BLOB_PATH)) {
        DEBUG_SERIAL.println("[storage] No key blob found");
        return false;
    }

    File f = LittleFS.open(KEY_BLOB_PATH, "r");
    if (!f) return false;

    if (f.size() != KEY_BLOB_TOTAL_SIZE) {
        DEBUG_SERIAL.printf("[storage] Key blob wrong size: %u\n", f.size());
        f.close();
        return false;
    }

    uint8_t nonce[KEY_BLOB_NONCE_SIZE];
    uint8_t tag[KEY_BLOB_TAG_SIZE];
    uint8_t ciphertext[KEY_BLOB_CIPHERTEXT_SIZE];

    f.read(nonce,      KEY_BLOB_NONCE_SIZE);
    f.read(tag,        KEY_BLOB_TAG_SIZE);
    f.read(ciphertext, KEY_BLOB_CIPHERTEXT_SIZE);
    f.close();

    // Derive wrapping key
    uint8_t wrap_key[32];
    if (!storage_derive_wrap_key(pin, wrap_key)) return false;

    // Decrypt + authenticate
    uint8_t plaintext[KEY_BLOB_PLAINTEXT_SIZE];
    bool ok = crypto_aes256gcm_decrypt(wrap_key, nonce, tag,
                                       ciphertext, KEY_BLOB_CIPHERTEXT_SIZE,
                                       plaintext);
    crypto_memzero(wrap_key, sizeof(wrap_key));

    if (!ok) {
        crypto_memzero(plaintext, sizeof(plaintext));
        DEBUG_SERIAL.println("[storage] AES-GCM auth failed (bad PIN or corruption)");
        return false;
    }

    // Unpack plaintext into KeyMaterial
    memcpy(km->master_seed, plaintext, 32);
    for (int i = 0; i < KEY_COUNT; i++) {
        memcpy(km->keys[i], plaintext + 32 + i * 32, 32);
    }

    crypto_memzero(plaintext, sizeof(plaintext));
    return true;
}

// ── Wipe keys ────────────────────────────────────────────────
// SECURITY: Called after MAX_PIN_ATTEMPTS failures.
// Overwrites the key blob with zeros before removing,
// formats LittleFS entirely, and resets the NVS counter.
bool storage_wipe_keys(void) {
    DEBUG_SERIAL.println("[storage] *** WIPING ALL KEYS ***");

    // If blob exists, overwrite with zeros before removing
    if (LittleFS.exists(KEY_BLOB_PATH)) {
        File f = LittleFS.open(KEY_BLOB_PATH, "r+");
        if (f) {
            uint8_t zeros[KEY_BLOB_TOTAL_SIZE];
            memset(zeros, 0, sizeof(zeros));
            f.seek(0);
            f.write(zeros, sizeof(zeros));
            f.flush();
            f.close();
        }
        LittleFS.remove(KEY_BLOB_PATH);
    }

    // Format LittleFS entirely
    LittleFS.format();
    LittleFS.begin(true);
    LittleFS.mkdir("/keys");

    // Reset attempt counter
    storage_reset_attempts();

    DEBUG_SERIAL.println("[storage] Wipe complete");
    return true;
}

// ── Key presence check ───────────────────────────────────────
bool storage_has_keys(void) {
    return LittleFS.exists(KEY_BLOB_PATH);
}

// ── NVS attempt counter ──────────────────────────────────────
// We use the NVS namespace "fibersigner" / key "pin_attempts".
// NVS persists across reboots and power cycles.

static nvs_handle_t s_nvs_handle = 0;

static bool nvs_open_handle(void) {
    if (s_nvs_handle != 0) return true;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        DEBUG_SERIAL.printf("[storage] NVS open failed: %d\n", err);
        return false;
    }
    return true;
}

int storage_get_attempts(void) {
    if (!nvs_open_handle()) return 0;
    int32_t count = 0;
    nvs_get_i32(s_nvs_handle, NVS_KEY_ATTEMPTS, &count);
    return (int)count;
}

int storage_increment_attempts(void) {
    if (!nvs_open_handle()) return -1;
    int32_t count = 0;
    nvs_get_i32(s_nvs_handle, NVS_KEY_ATTEMPTS, &count);
    count++;
    nvs_set_i32(s_nvs_handle, NVS_KEY_ATTEMPTS, count);
    nvs_commit(s_nvs_handle);
    DEBUG_SERIAL.printf("[storage] PIN attempts: %d\n", (int)count);
    return (int)count;
}

void storage_reset_attempts(void) {
    if (!nvs_open_handle()) return;
    nvs_set_i32(s_nvs_handle, NVS_KEY_ATTEMPTS, 0);
    nvs_commit(s_nvs_handle);
    DEBUG_SERIAL.println("[storage] Attempt counter reset");
}
