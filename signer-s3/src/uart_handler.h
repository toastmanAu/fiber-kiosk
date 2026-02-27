#pragma once
// ============================================================
// uart_handler.h — JSON-RPC UART Handler
// Reads newline-delimited JSON from Serial, dispatches methods,
// writes JSON response + newline.
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "signer_protocol.h"
#include "crypto.h"
#include "storage.h"
#include "musig2.h"

// Maximum input line length (same as MAX_FRAME_SIZE)
#define INPUT_BUF_SIZE  MAX_FRAME_SIZE

// ── Signer state ─────────────────────────────────────────────
// Global state shared across handlers.
struct SignerState {
    bool        unlocked;           // True if PIN was provided and keys loaded
    bool        has_keys;           // True if key blob exists in flash
    KeyMaterial km;                 // Decrypted key material (only valid when unlocked)
    char        pin_buf[MAX_PIN_LEN + 1]; // Currently set PIN (for key gen on first boot)
    unsigned long unlock_time_ms;   // When unlock happened (for session timeout)
};

extern SignerState g_signer;

// ── Init ─────────────────────────────────────────────────────
// Call once from setup(). Checks for existing keys, updates state.
void uart_handler_init(void);

// ── Main loop tick ───────────────────────────────────────────
// Call from loop(). Reads one line if available, processes it.
void uart_handler_tick(void);

// ── RPC dispatch ─────────────────────────────────────────────
// Process a complete JSON line. Writes response to RPC_SERIAL.
void uart_handler_dispatch(const char *json_line, size_t len);

// ── Method handlers ──────────────────────────────────────────
// Each handler receives the parsed doc and request id.
// Returns true on success (result object already populated).

void handle_unlock(JsonDocument &doc, int id, JsonDocument &resp);
void handle_lock(JsonDocument &doc, int id, JsonDocument &resp);
void handle_get_status(JsonDocument &doc, int id, JsonDocument &resp);
void handle_get_pubkey(JsonDocument &doc, int id, JsonDocument &resp);
void handle_sign_tx(JsonDocument &doc, int id, JsonDocument &resp);
void handle_sign_htlc(JsonDocument &doc, int id, JsonDocument &resp);
void handle_musig2_round1(JsonDocument &doc, int id, JsonDocument &resp);
void handle_musig2_round2(JsonDocument &doc, int id, JsonDocument &resp);
void handle_generate_keys(JsonDocument &doc, int id, JsonDocument &resp);

// ── Response helpers ─────────────────────────────────────────
void send_error(int id, int code, const char *message);
void send_response(JsonDocument &resp);
