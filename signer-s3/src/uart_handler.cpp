// ============================================================
// uart_handler.cpp — JSON-RPC UART Handler Implementation
// All signer RPC methods handled here.
// ============================================================

#include "uart_handler.h"
#include <string.h>
#include <Arduino.h>
#include "esp_random.h"

// ── Serial port configuration ────────────────────────────────
// RPC_SERIAL: UART0 at 921600 (JSON-RPC channel)
// DEBUG_SERIAL: USB CDC (debug output)
#ifndef RPC_SERIAL
#define RPC_SERIAL Serial0
#endif
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL Serial
#endif

// ── Global signer state ──────────────────────────────────────
SignerState g_signer = {
    .unlocked       = false,
    .has_keys       = false,
    .km             = {},
    .pin_buf        = {},
    .unlock_time_ms = 0,
};

// Line buffer
static char s_line_buf[INPUT_BUF_SIZE];
static size_t s_line_pos = 0;

// ── Init ─────────────────────────────────────────────────────
void uart_handler_init(void) {
    g_signer.has_keys = storage_has_keys();
    g_signer.unlocked = false;
    memset(&g_signer.km, 0, sizeof(g_signer.km));
    musig2_init();

    DEBUG_SERIAL.printf("[rpc] init: has_keys=%d\n", (int)g_signer.has_keys);
}

// ── Response helpers ─────────────────────────────────────────
void send_error(int id, int code, const char *message) {
    // Write directly to avoid large stack allocation
    char buf[256];
    if (id >= 0) {
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                 id, code, message);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
                 code, message);
    }
    RPC_SERIAL.print(buf);
    DEBUG_SERIAL.printf("[rpc] error: code=%d msg=%s\n", code, message);
}

void send_response(JsonDocument &resp) {
    String out;
    serializeJson(resp, out);
    out += "\n";
    RPC_SERIAL.print(out);
    DEBUG_SERIAL.print("[rpc] resp: ");
    DEBUG_SERIAL.print(out);
}

// ── Main loop tick ───────────────────────────────────────────
void uart_handler_tick(void) {
    while (RPC_SERIAL.available()) {
        char c = (char)RPC_SERIAL.read();
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                uart_handler_dispatch(s_line_buf, s_line_pos);
                s_line_pos = 0;
            }
        } else {
            if (s_line_pos < INPUT_BUF_SIZE - 1) {
                s_line_buf[s_line_pos++] = c;
            } else {
                // Line too long: discard and send error
                s_line_pos = 0;
                send_error(-1, JSONRPC_PARSE_ERROR, "Frame too large");
            }
        }
    }
}

// ── Dispatch ─────────────────────────────────────────────────
void uart_handler_dispatch(const char *json_line, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_line, len);
    if (err) {
        send_error(-1, JSONRPC_PARSE_ERROR, "JSON parse error");
        return;
    }

    int id = doc["id"] | -1;
    const char *method = doc["method"] | "";

    if (!method || strlen(method) == 0) {
        send_error(id, JSONRPC_INVALID_REQUEST, "Missing method");
        return;
    }

    DEBUG_SERIAL.printf("[rpc] method=%s id=%d\n", method, id);

    JsonDocument resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;

    if (strcmp(method, METHOD_UNLOCK) == 0) {
        handle_unlock(doc, id, resp);
    } else if (strcmp(method, METHOD_LOCK) == 0) {
        handle_lock(doc, id, resp);
    } else if (strcmp(method, METHOD_GET_STATUS) == 0) {
        handle_get_status(doc, id, resp);
    } else if (strcmp(method, METHOD_GET_PUBKEY) == 0) {
        handle_get_pubkey(doc, id, resp);
    } else if (strcmp(method, METHOD_SIGN_TX) == 0) {
        handle_sign_tx(doc, id, resp);
    } else if (strcmp(method, METHOD_SIGN_HTLC) == 0) {
        handle_sign_htlc(doc, id, resp);
    } else if (strcmp(method, METHOD_MUSIG2_ROUND1) == 0) {
        handle_musig2_round1(doc, id, resp);
    } else if (strcmp(method, METHOD_MUSIG2_ROUND2) == 0) {
        handle_musig2_round2(doc, id, resp);
    } else if (strcmp(method, METHOD_GENERATE_KEYS) == 0) {
        handle_generate_keys(doc, id, resp);
    } else {
        send_error(id, JSONRPC_METHOD_NOT_FOUND, "Unknown method");
    }
}

// ── unlock ───────────────────────────────────────────────────
// {"method":"unlock","params":{"pin":"1234"}}
// Security: load keys with provided PIN.
// On failure, increment attempt counter.
// On 5th failure, wipe all keys.
void handle_unlock(JsonDocument &doc, int id, JsonDocument &resp) {
    const char *pin = doc["params"]["pin"] | "";
    if (!pin || strlen(pin) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing pin");
        return;
    }

    if (!g_signer.has_keys) {
        send_error(id, SIGNER_ERR_NO_KEYS, "No keys present; call generate_keys first");
        return;
    }

    // Check attempt count before trying (double-check after NVS read)
    int attempts = storage_get_attempts();
    if (attempts >= MAX_PIN_ATTEMPTS) {
        // Already exceeded; wipe and report
        storage_wipe_keys();
        g_signer.has_keys = false;
        g_signer.unlocked = false;
        send_error(id, SIGNER_ERR_WIPED, "Device wiped after too many failed attempts");
        return;
    }

    // Try to decrypt keys
    KeyMaterial km_tmp;
    bool ok = storage_load_keys(&km_tmp, pin);

    if (!ok) {
        int new_attempts = storage_increment_attempts();
        int remaining = MAX_PIN_ATTEMPTS - new_attempts;

        if (new_attempts >= MAX_PIN_ATTEMPTS) {
            // Wipe on 5th failure
            storage_wipe_keys();
            g_signer.has_keys = false;
            g_signer.unlocked = false;
            crypto_memzero(&km_tmp, sizeof(km_tmp));
            send_error(id, SIGNER_ERR_WIPED,
                       "Device wiped after 5 failed PIN attempts");
            return;
        }

        crypto_memzero(&km_tmp, sizeof(km_tmp));
        char msg[80];
        snprintf(msg, sizeof(msg), "Wrong PIN. %d attempt%s remaining.",
                 remaining, remaining == 1 ? "" : "s");
        send_error(id, SIGNER_ERR_BAD_PIN, msg);
        return;
    }

    // Success
    storage_reset_attempts();
    memcpy(&g_signer.km, &km_tmp, sizeof(KeyMaterial));
    crypto_memzero(&km_tmp, sizeof(km_tmp));
    g_signer.unlocked = true;
    g_signer.unlock_time_ms = millis();

    resp["result"]["unlocked"] = true;
    resp["result"]["attempts_remaining"] = MAX_PIN_ATTEMPTS;
    send_response(resp);
}

// ── lock ─────────────────────────────────────────────────────
void handle_lock(JsonDocument &doc, int id, JsonDocument &resp) {
    if (g_signer.unlocked) {
        crypto_memzero(&g_signer.km, sizeof(g_signer.km));
        g_signer.unlocked = false;
        musig2_clear_all_sessions();
    }
    resp["result"]["locked"] = true;
    send_response(resp);
}

// ── get_status ───────────────────────────────────────────────
void handle_get_status(JsonDocument &doc, int id, JsonDocument &resp) {
    resp["result"]["unlocked"]  = g_signer.unlocked;
    resp["result"]["has_keys"]  = g_signer.has_keys;
    resp["result"]["firmware"]  = FIRMWARE_VERSION;
    resp["result"]["board"]     = BOARD_NAME;
    resp["result"]["attempts"]  = storage_get_attempts();

    if (g_signer.unlocked) {
        char pubkey_hex[67];
        uint8_t pubkey[33];
        if (crypto_pubkey(g_signer.km.keys[KEY_IDENTITY], pubkey)) {
            bytes_to_hex(pubkey, 33, pubkey_hex);
            resp["result"]["node_pubkey"] = pubkey_hex;
        }
    }
    send_response(resp);
}

// ── get_pubkey ───────────────────────────────────────────────
// {"method":"get_pubkey","params":{"key_index":0}}
// key_index: 0=identity, 1=funding, 2=revocation, 3=htlc, 4=payment
void handle_get_pubkey(JsonDocument &doc, int id, JsonDocument &resp) {
    if (!g_signer.unlocked) {
        send_error(id, SIGNER_ERR_LOCKED, "Signer is locked");
        return;
    }

    int key_index = doc["params"]["key_index"] | -1;
    if (key_index < 0 || key_index >= KEY_COUNT) {
        send_error(id, SIGNER_ERR_INVALID_KEY, "key_index must be 0..4");
        return;
    }

    uint8_t pubkey[33];
    if (!crypto_pubkey(g_signer.km.keys[key_index], pubkey)) {
        send_error(id, SIGNER_ERR_CRYPTO, "Failed to compute public key");
        return;
    }

    char pubkey_hex[67];
    bytes_to_hex(pubkey, 33, pubkey_hex);

    resp["result"]["pubkey"]    = pubkey_hex;
    resp["result"]["key_index"] = key_index;
    send_response(resp);
}

// ── sign_tx ──────────────────────────────────────────────────
// {"method":"sign_tx","params":{"tx_hash":"0x...","key_index":1}}
void handle_sign_tx(JsonDocument &doc, int id, JsonDocument &resp) {
    if (!g_signer.unlocked) {
        send_error(id, SIGNER_ERR_LOCKED, "Signer is locked");
        return;
    }

    const char *tx_hash_hex = doc["params"]["tx_hash"] | "";
    int key_index = doc["params"]["key_index"] | -1;

    if (!tx_hash_hex || strlen(tx_hash_hex) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing tx_hash");
        return;
    }
    if (key_index < 0 || key_index >= KEY_COUNT) {
        send_error(id, SIGNER_ERR_INVALID_KEY, "key_index must be 0..4");
        return;
    }

    // Parse tx_hash (32 bytes)
    uint8_t hash[32];
    if (hex_to_bytes(tx_hash_hex, hash, 32) != 32) {
        send_error(id, JSONRPC_INVALID_PARAMS, "tx_hash must be 32 bytes hex");
        return;
    }

    uint8_t sig[64];
    if (!crypto_sign(g_signer.km.keys[key_index], hash, sig)) {
        send_error(id, SIGNER_ERR_CRYPTO, "Signing failed");
        return;
    }

    char sig_hex[130]; // 64*2 + "0x" + NUL
    char sig_hex_raw[129];
    bytes_to_hex(sig, 64, sig_hex_raw);
    snprintf(sig_hex, sizeof(sig_hex), "0x%s", sig_hex_raw);

    resp["result"]["signature"] = sig_hex;
    resp["result"]["key_index"] = key_index;
    send_response(resp);
}

// ── sign_htlc ────────────────────────────────────────────────
// {"method":"sign_htlc","params":{"htlc_tx_hash":"0x...","key_index":3}}
void handle_sign_htlc(JsonDocument &doc, int id, JsonDocument &resp) {
    if (!g_signer.unlocked) {
        send_error(id, SIGNER_ERR_LOCKED, "Signer is locked");
        return;
    }

    const char *htlc_hash_hex = doc["params"]["htlc_tx_hash"] | "";
    int key_index = doc["params"]["key_index"] | KEY_HTLC;

    if (!htlc_hash_hex || strlen(htlc_hash_hex) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing htlc_tx_hash");
        return;
    }
    if (key_index < 0 || key_index >= KEY_COUNT) {
        send_error(id, SIGNER_ERR_INVALID_KEY, "key_index must be 0..4");
        return;
    }

    uint8_t hash[32];
    if (hex_to_bytes(htlc_hash_hex, hash, 32) != 32) {
        send_error(id, JSONRPC_INVALID_PARAMS, "htlc_tx_hash must be 32 bytes hex");
        return;
    }

    // CKB HTLC signing: Blake2b-hash the message before signing (CKB convention)
    uint8_t blake_hash[32];
    if (!crypto_blake2b(hash, 32, blake_hash)) {
        send_error(id, SIGNER_ERR_CRYPTO, "Blake2b hash failed");
        return;
    }

    uint8_t sig[64];
    if (!crypto_sign(g_signer.km.keys[key_index], blake_hash, sig)) {
        send_error(id, SIGNER_ERR_CRYPTO, "HTLC signing failed");
        return;
    }

    char sig_hex[130];
    char sig_hex_raw[129];
    bytes_to_hex(sig, 64, sig_hex_raw);
    snprintf(sig_hex, sizeof(sig_hex), "0x%s", sig_hex_raw);

    resp["result"]["signature"] = sig_hex;
    resp["result"]["key_index"] = key_index;
    send_response(resp);
}

// ── musig2_round1 ────────────────────────────────────────────
// {"method":"musig2_round1","params":{"session_id":"hex...","key_index":1,"message":"0x..."}}
void handle_musig2_round1(JsonDocument &doc, int id, JsonDocument &resp) {
    if (!g_signer.unlocked) {
        send_error(id, SIGNER_ERR_LOCKED, "Signer is locked");
        return;
    }

    const char *session_id = doc["params"]["session_id"] | "";
    const char *message_hex = doc["params"]["message"] | "";
    int key_index = doc["params"]["key_index"] | KEY_FUNDING;

    if (!session_id || strlen(session_id) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing session_id");
        return;
    }
    if (key_index < 0 || key_index >= KEY_COUNT) {
        send_error(id, SIGNER_ERR_INVALID_KEY, "key_index must be 0..4");
        return;
    }

    uint8_t message[32] = {};
    if (message_hex && strlen(message_hex) > 0) {
        if (hex_to_bytes(message_hex, message, 32) != 32) {
            send_error(id, JSONRPC_INVALID_PARAMS, "message must be 32 bytes hex");
            return;
        }
    }

    char R1_hex[67], R2_hex[67];
    if (!musig2_round1(session_id, g_signer.km.keys[key_index], message,
                       R1_hex, R2_hex)) {
        send_error(id, SIGNER_ERR_CRYPTO, "MuSig2 round1 failed");
        return;
    }

    // pubnonce = R1 || R2 (66 bytes hex = 132 chars)
    char pubnonce[135];
    snprintf(pubnonce, sizeof(pubnonce), "0x%s%s", R1_hex, R2_hex);

    resp["result"]["pubnonce"]   = pubnonce;
    resp["result"]["session_id"] = session_id;
    send_response(resp);
}

// ── musig2_round2 ────────────────────────────────────────────
// {"method":"musig2_round2","params":{
//   "session_id":"hex", "agg_pubnonce":"0x...", "message":"0x...",
//   "counterparty_pubkey":"0x02..."}}
void handle_musig2_round2(JsonDocument &doc, int id, JsonDocument &resp) {
    if (!g_signer.unlocked) {
        send_error(id, SIGNER_ERR_LOCKED, "Signer is locked");
        return;
    }

    const char *session_id      = doc["params"]["session_id"]           | "";
    const char *agg_pubnonce    = doc["params"]["agg_pubnonce"]         | "";
    const char *message_hex     = doc["params"]["message"]              | "";
    const char *cp_pubkey       = doc["params"]["counterparty_pubkey"]  | "";

    if (!session_id || strlen(session_id) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing session_id");
        return;
    }
    if (!agg_pubnonce || strlen(agg_pubnonce) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing agg_pubnonce");
        return;
    }
    if (!message_hex || strlen(message_hex) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing message");
        return;
    }
    if (!cp_pubkey || strlen(cp_pubkey) == 0) {
        send_error(id, JSONRPC_INVALID_PARAMS, "Missing counterparty_pubkey");
        return;
    }

    char partial_sig[65];
    if (!musig2_round2(session_id, agg_pubnonce, message_hex, cp_pubkey, partial_sig)) {
        send_error(id, SIGNER_ERR_CRYPTO, "MuSig2 round2 failed");
        return;
    }

    char partial_sig_with_prefix[68];
    snprintf(partial_sig_with_prefix, sizeof(partial_sig_with_prefix), "0x%s", partial_sig);

    resp["result"]["partial_sig"] = partial_sig_with_prefix;
    resp["result"]["session_id"]  = session_id;
    send_response(resp);
}

// ── generate_keys ────────────────────────────────────────────
// First-boot key generation.
// {"method":"generate_keys","params":{"pin":"123456"}}
// Only works if no keys exist.
void handle_generate_keys(JsonDocument &doc, int id, JsonDocument &resp) {
    if (g_signer.has_keys) {
        send_error(id, JSONRPC_INVALID_REQUEST,
                   "Keys already exist; wipe device to regenerate");
        return;
    }

    const char *pin = doc["params"]["pin"] | "";
    if (!pin || strlen(pin) < 4) {
        send_error(id, JSONRPC_INVALID_PARAMS, "PIN must be at least 4 characters");
        return;
    }

    KeyMaterial km_tmp;
    if (!crypto_generate_keys(&km_tmp)) {
        send_error(id, SIGNER_ERR_CRYPTO, "Key generation failed (RNG error?)");
        return;
    }

    if (!storage_store_keys(&km_tmp, pin)) {
        crypto_memzero(&km_tmp, sizeof(km_tmp));
        send_error(id, SIGNER_ERR_CRYPTO, "Failed to store keys to flash");
        return;
    }

    // Auto-unlock after key generation
    memcpy(&g_signer.km, &km_tmp, sizeof(KeyMaterial));
    crypto_memzero(&km_tmp, sizeof(km_tmp));

    g_signer.has_keys = true;
    g_signer.unlocked = true;
    g_signer.unlock_time_ms = millis();
    storage_reset_attempts();

    // Return identity pubkey
    uint8_t pubkey[33];
    char pubkey_hex[67];
    crypto_pubkey(g_signer.km.keys[KEY_IDENTITY], pubkey);
    bytes_to_hex(pubkey, 33, pubkey_hex);

    resp["result"]["generated"]   = true;
    resp["result"]["unlocked"]    = true;
    resp["result"]["node_pubkey"] = pubkey_hex;
    send_response(resp);
}
