# ESP32-S3 Fiber Channel Signer

A cost-reduced hardware signing device for the [Nervos CKB Fiber Network](https://github.com/nervosnetwork/fiber).

This is the **ESP32-S3 variant** of the Fiber Signer — same protocol as the ESP32-P4 version, lower cost (~$5–8 vs ~$15–20), perfect for MVP/development deployments.

---

## Hardware

| Board | ESP32-S3-DevKitC-1 (or compatible S3 module) |
|-------|----------------------------------------------|
| CPU   | Xtensa LX7 dual-core @ 240 MHz |
| Flash | 4–16 MB (8 MB recommended) |
| PSRAM | 2–8 MB QSPI (optional but recommended) |
| HW Crypto | AES-256, SHA-256/512, hardware RNG |
| USB   | Native USB OTG (acts as CDC serial to host) |
| Price | ~$5–8 |

**GPIO Pinout for UART0 (JSON-RPC channel):**

| Function | ESP32-S3 GPIO |
|----------|--------------|
| UART0 TX | GPIO 43 |
| UART0 RX | GPIO 44 |
| GND      | GND |

---

## Wiring to Orange Pi 3B (or Raspberry Pi)

Connect UART0 to the OPi's UART pins with **crossed TX/RX**:

```
ESP32-S3          Orange Pi 3B
─────────         ────────────
GPIO43 (TX) ───► UART RX  (e.g. /dev/ttyS5 pin 8)
GPIO44 (RX) ◄─── UART TX  (e.g. /dev/ttyS5 pin 10)
GND         ───── GND
```

> **⚠️ Voltage:** ESP32-S3 is 3.3 V logic. Orange Pi is also 3.3 V on UART. **Do NOT connect to 5 V UART** without a level shifter.

Alternatively, use **USB CDC**: plug the ESP32-S3's native USB port directly into any USB-A port on the OPi. It appears as `/dev/ttyACM0`. No wiring needed.

---

## Prerequisites

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
2. Clone this repo:
   ```bash
   git clone https://github.com/toastmanAu/fiber-kiosk
   cd fiber-kiosk/signer-s3
   ```
3. PlatformIO will auto-install dependencies on first build.

---

## Build & Flash

```bash
# Build
pio run

# Flash (auto-detects USB port)
pio run --target upload

# Monitor debug output (USB CDC)
pio device monitor --baud 115200

# Flash + monitor in one step
pio run --target upload && pio device monitor --baud 115200
```

If your board isn't detected, specify the port:
```bash
pio run --target upload --upload-port /dev/ttyACM0
```

---

## First Boot

On first boot, the device has no keys. You must generate them:

### 1. Connect to the RPC UART

```bash
# Via USB CDC (easiest)
screen /dev/ttyACM0 921600

# Or with minicom
minicom -D /dev/ttyACM0 -b 921600
```

### 2. Generate keys with a PIN

Send this JSON (followed by newline):

```json
{"jsonrpc":"2.0","id":1,"method":"generate_keys","params":{"pin":"your-pin-here"}}
```

Response:
```json
{"jsonrpc":"2.0","id":1,"result":{"generated":true,"unlocked":true,"node_pubkey":"02abc..."}}
```

**Save the `node_pubkey`** — this is your Fiber node's identity public key.

### 3. Pin requirements

- Minimum 4 characters
- Recommended: 8+ character alphanumeric PIN
- **Write it down somewhere safe** — if lost, the device cannot be unlocked
- After **5 wrong PIN attempts**, all keys are permanently wiped

---

## JSON-RPC Protocol

The signer speaks newline-delimited JSON-RPC 2.0 over UART0 at 921600 baud.

### unlock

```json
{"jsonrpc":"2.0","id":1,"method":"unlock","params":{"pin":"1234"}}
```

```json
{"jsonrpc":"2.0","id":1,"result":{"unlocked":true,"attempts_remaining":5}}
```

Failed unlock (wrong PIN):
```json
{"jsonrpc":"2.0","id":1,"error":{"code":2,"message":"Wrong PIN. 4 attempts remaining."}}
```

Device wiped (5 failures):
```json
{"jsonrpc":"2.0","id":1,"error":{"code":3,"message":"Device wiped after 5 failed PIN attempts"}}
```

### lock

```json
{"jsonrpc":"2.0","id":2,"method":"lock"}
```

```json
{"jsonrpc":"2.0","id":2,"result":{"locked":true}}
```

### get_status

No PIN required.

```json
{"jsonrpc":"2.0","id":3,"method":"get_status"}
```

```json
{"jsonrpc":"2.0","id":3,"result":{"unlocked":false,"has_keys":true,"firmware":"s3-signer-v0.1","board":"esp32-s3","attempts":0}}
```

### get_pubkey

```json
{"jsonrpc":"2.0","id":4,"method":"get_pubkey","params":{"key_index":0}}
```

```json
{"jsonrpc":"2.0","id":4,"result":{"pubkey":"02abc...","key_index":0}}
```

**Key index mapping:**

| key_index | Key | Usage |
|-----------|-----|-------|
| 0 | Identity key (`m/fiber/0`) | Peer auth, gossip |
| 1 | Funding key (`m/fiber/1`) | Channel funding, MuSig2 |
| 2 | Revocation key (`m/fiber/2`) | Commitment revocation |
| 3 | HTLC key (`m/fiber/3`) | HTLC signing |
| 4 | Payment key (`m/fiber/4`) | Invoice preimage |

### sign_tx

```json
{"jsonrpc":"2.0","id":5,"method":"sign_tx","params":{"tx_hash":"0xabc...","key_index":1}}
```

```json
{"jsonrpc":"2.0","id":5,"result":{"signature":"0xdef...","key_index":1}}
```

### sign_htlc

Same as `sign_tx` but applies Blake2b pre-hashing (CKB HTLC convention).

```json
{"jsonrpc":"2.0","id":6,"method":"sign_htlc","params":{"htlc_tx_hash":"0xabc...","key_index":3}}
```

```json
{"jsonrpc":"2.0","id":6,"result":{"signature":"0xdef...","key_index":3}}
```

### musig2_round1

```json
{"jsonrpc":"2.0","id":7,"method":"musig2_round1","params":{
  "session_id":"abcdef01",
  "key_index":1,
  "message":"0x1234..."
}}
```

```json
{"jsonrpc":"2.0","id":7,"result":{
  "pubnonce":"0x02abc...03def...",
  "session_id":"abcdef01"
}}
```

### musig2_round2

```json
{"jsonrpc":"2.0","id":8,"method":"musig2_round2","params":{
  "session_id":"abcdef01",
  "agg_pubnonce":"0x02abc...03def...",
  "message":"0x1234...",
  "counterparty_pubkey":"0x03xyz..."
}}
```

```json
{"jsonrpc":"2.0","id":8,"result":{
  "partial_sig":"0x1234...",
  "session_id":"abcdef01"
}}
```

---

## Error Codes

| Code | Name | Meaning |
|------|------|---------|
| -32700 | PARSE_ERROR | Invalid JSON |
| -32600 | INVALID_REQUEST | Malformed request |
| -32601 | METHOD_NOT_FOUND | Unknown method |
| -32602 | INVALID_PARAMS | Missing/invalid parameters |
| 1 | LOCKED | Signer locked; call unlock first |
| 2 | BAD_PIN | Wrong PIN |
| 3 | WIPED | Device wiped after 5 failed attempts |
| 4 | CRYPTO | Crypto operation failed |
| 5 | NO_KEYS | No keys present; call generate_keys |
| 6 | INVALID_KEY | key_index out of range (0..4) |
| 7 | SESSION | MuSig2 session not found |

---

## Companion Bridge Daemon

Run this on the OPi 3B alongside `fnn`:

```python
#!/usr/bin/env python3
# fiber-signer-bridge.py — bridges fnn to ESP32-S3 signer

import serial, json, socket, os, sys

SIGNER_PORT = '/dev/ttyACM0'   # USB CDC, or /dev/ttyS5 for GPIO UART
SOCKET_PATH = '/tmp/fiber-signer.sock'
BAUD = 921600

ser = serial.Serial(SIGNER_PORT, BAUD, timeout=5)

if os.path.exists(SOCKET_PATH):
    os.remove(SOCKET_PATH)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.bind(SOCKET_PATH)
sock.listen(1)

print(f'Fiber signer bridge ready on {SOCKET_PATH}')

while True:
    conn, _ = sock.accept()
    try:
        data = conn.recv(4096)
        req = json.loads(data)
        line = json.dumps(req).encode() + b'\n'
        ser.write(line)
        resp_line = ser.readline()
        conn.send(resp_line)
    except Exception as e:
        print(f'Bridge error: {e}')
    finally:
        conn.close()
```

```bash
# Install pyserial if needed
pip install pyserial

# Run alongside fnn
python3 fiber-signer-bridge.py &
```

---

## Security Model

- **Private keys never leave the device** — only signatures and public keys are returned
- **PIN gate** — signer stays locked until unlocked with correct PIN
- **5-attempt lockout** — after 5 wrong PINs, all keys are permanently wiped
- **AES-256-GCM** — keys encrypted at rest using PIN + efuse device ID as key material
- **HKDF-SHA256** — wrap key derived from PIN + chip hardware ID (binds encryption to this specific chip)
- **Hardware RNG** — ESP32-S3 hardware entropy used for key generation and nonces
- **Auto-lock** — signer auto-locks after 5 minutes of inactivity

### What an attacker can't do

- **Flash dump + brute force** — keys are encrypted with PIN+efuse; useless without both
- **UART sniffing** — signatures are not secret; keys never appear on the wire
- **Replay attacks** — secp256k1 with RFC6979 deterministic nonces prevents nonce reuse

### What the S3 signer does NOT protect against

- User confirming a malicious transaction (social engineering)
- Physical access with unlimited time + sophisticated fault injection
- Side-channel attacks (timing, power) — out of scope for v0.1

---

## S3 vs P4 Signer Comparison

| Feature | **ESP32-S3** (this) | ESP32-P4 |
|---------|---------------------|----------|
| CPU | Xtensa LX7 240MHz | RISC-V 400MHz |
| HW SHA accel | ✅ SHA-256/512 | ✅ |
| HW AES accel | ✅ AES-128/256 | ✅ |
| HW RNG | ✅ | ✅ |
| RSA HW | ❌ (software) | ✅ |
| PSRAM | 2–8 MB | 32 MB |
| Price | **~$5–8** | ~$15–20 |
| WiFi | ✅ 2.4 GHz | ✅ |
| USB OTG | ✅ Native | ✅ |
| **Recommended for** | **MVP / budget** | Production |

The S3 provides the same secp256k1 + AES-256-GCM security as the P4 signer. The main differences are price and compute throughput — MuSig2 on S3 is slightly slower but well within Fiber's timing requirements.

---

## Key Storage Format

```
LittleFS: /keys/master.bin (220 bytes)

Plaintext (192 bytes):
  [  0: 32]  master_seed              (raw entropy)
  [ 32: 64]  fiber_identity_key       m/fiber/0
  [ 64: 96]  fiber_funding_key        m/fiber/1
  [ 96:128]  fiber_revocation_key     m/fiber/2
  [128:160]  fiber_htlc_key           m/fiber/3
  [160:192]  fiber_payment_key        m/fiber/4

Ciphertext format: [nonce:12][tag:16][ciphertext:192] = 220 bytes
Encryption: AES-256-GCM
Wrap key: HKDF-SHA256(PIN || efuse_device_id, salt="fiber-signer-v1")
```

NVS namespace `fibersigner`:
- `pin_attempts` (i32) — persists across reboots; wiped on successful unlock or wipe

---

## Directory Structure

```
signer-s3/
├── platformio.ini              PlatformIO config (ESP32-S3, huge_app partition)
├── README.md                   This file
└── src/
    ├── main.cpp                Entry point, setup/loop
    ├── signer_protocol.h       RPC method names + error codes
    ├── crypto.h / crypto.cpp   secp256k1, Blake2b, HKDF, AES-GCM
    ├── storage.h / storage.cpp LittleFS + NVS key management
    ├── uart_handler.h / .cpp   JSON-RPC dispatcher + method handlers
    ├── musig2.h / musig2.cpp   MuSig2 two-round partial signing
    ├── trezor_crypto/          secp256k1 + SHA2 + HMAC (from trezor-crypto)
    └── blake2b/                Blake2b (CKB standard hash)
```

---

*Fiber Signer S3 — built for the Nervos CKB Fiber Network*
*Repository: [toastmanAu/fiber-kiosk](https://github.com/toastmanAu/fiber-kiosk)*
