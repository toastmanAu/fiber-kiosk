// ============================================================
// main.cpp — ESP32-S3 Fiber Channel Signer
// Cost-reduced variant of the ESP32-P4 Fiber Signer.
// JSON-RPC over UART0 (921600 baud) + USB CDC debug output.
// ============================================================

#include <Arduino.h>
#include "signer_protocol.h"
#include "crypto.h"
#include "storage.h"
#include "uart_handler.h"

// ── RPC Serial: UART0 at 921600 baud ────────────────────────
// This is the channel the Fiber node (OPi 3B) communicates on.
// On ESP32-S3-DevKitC-1: GPIO43=TX0, GPIO44=RX0
#define RPC_BAUD 921600

// ── Debug Serial: USB CDC ────────────────────────────────────
// USB-CDC is the native USB port on ESP32-S3.
// Enabled by: -DARDUINO_USB_CDC_ON_BOOT=1 in platformio.ini
// Connect via USB for human-readable debug output.
#define DEBUG_BAUD 115200

// ── Session auto-lock (5 minutes) ────────────────────────────
#define AUTO_LOCK_MS  (5UL * 60UL * 1000UL)

// ── Setup ────────────────────────────────────────────────────
void setup() {
    // USB CDC for debug (must initialise first for early messages)
    Serial.begin(DEBUG_BAUD);
    // Give USB CDC time to enumerate (skip on production if speed matters)
    delay(500);

    Serial.println("==============================================");
    Serial.println("  Fiber Channel Signer — ESP32-S3");
    Serial.printf ("  Firmware: %s\n", FIRMWARE_VERSION);
    Serial.println("==============================================");

    // UART0 for JSON-RPC
    Serial0.begin(RPC_BAUD, SERIAL_8N1, 44, 43); // RX=GPIO44, TX=GPIO43
    Serial.printf("[main] RPC UART0 @ %d baud\n", RPC_BAUD);

    // Crypto subsystem
    if (!crypto_init()) {
        Serial.println("[main] FATAL: crypto_init failed");
        // Halt — cannot operate without crypto
        while (true) { delay(1000); }
    }
    Serial.println("[main] Crypto OK");

    // Storage (LittleFS + NVS)
    if (!storage_init()) {
        Serial.println("[main] FATAL: storage_init failed");
        while (true) { delay(1000); }
    }
    Serial.println("[main] Storage OK");

    // UART handler
    uart_handler_init();

    if (g_signer.has_keys) {
        Serial.println("[main] Key blob found — send 'unlock' to use signer");
    } else {
        Serial.println("[main] No keys found — send 'generate_keys' to initialise");
    }

    Serial.println("[main] Ready — waiting for JSON-RPC commands on UART0");
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
    // Process any incoming UART data
    uart_handler_tick();

    // Auto-lock after inactivity timeout
    if (g_signer.unlocked) {
        unsigned long now = millis();
        if ((now - g_signer.unlock_time_ms) > AUTO_LOCK_MS) {
            Serial.println("[main] Auto-lock: session timed out");
            crypto_memzero(&g_signer.km, sizeof(g_signer.km));
            g_signer.unlocked = false;
            musig2_clear_all_sessions();
        }
    }

    // Feed the watchdog (ESP32-S3 has WDT by default)
    delay(1);
}
