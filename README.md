# fiber-kiosk

Standalone Fiber Network touchscreen kiosk for OPi 3B (RK3566, 800×480 DSI).

No browser. Native C + LVGL 9 UI, direct framebuffer.

## Stack

```
fiber-kiosk (C/LVGL)          ← touchscreen UI
    ↕ HTTP localhost:7777
fiber-bridge (Node.js)        ← Fiber RPC + signer bridge
    ↕ Biscuit auth JSON-RPC
fnn (Fiber full node)         ← channel state, routing, peers
    ↕ UART /dev/ttyACM0
ESP32-P4 signer               ← private keys, MuSig2, HTLC signing
```

## Screens

| Screen | Description |
|--------|-------------|
| HOME | Node status, total balance, SEND/RECEIVE buttons |
| CHANNELS | List open channels, tap to select for sending |
| SEND | Invoice entry, numpad, channel info |
| CONFIRM | Sign request details, fee, signer confirmation |
| RECEIVE | Amount entry, generate invoice, QR code |
| SIGNER | Signer status, PIN unlock, firmware info |

## Quick Start (OPi 3B)

```bash
sudo bash scripts/setup.sh

# Edit Biscuit token
sudo nano /etc/fiber-kiosk.env

# Start
sudo systemctl start fiber-bridge fiber-kiosk
sudo journalctl -u fiber-kiosk -f
```

## Manual Run

```bash
# Terminal 1 — bridge
cd fiber-bridge && FIBER_TOKEN=<token> node server.js

# Terminal 2 — kiosk
sudo ./fiber-kiosk --fb /dev/fb0 --touch /dev/input/event0
```

## Hardware

| Component | Role |
|-----------|------|
| OPi 3B (RK3566) | Main compute, runs fnn + kiosk |
| DSI 800×480 touch | UI display (already working) |
| NVMe M.2 | RocksDB storage for Fiber channel state |
| ESP32-P4 (USB) | Hardware signer — keys never leave device |

## Config `/etc/fiber-kiosk.conf`

```ini
bridge_url      = http://127.0.0.1:7777
signer_port     = /dev/ttyACM0
fb_device       = /dev/fb0
touch_device    = /dev/input/event0
display_w       = 800
display_h       = 480
poll_ms         = 3000
pin_timeout_sec = 300
```

## Signer

The ESP32-P4 signer uses the protocol defined in `../CKB-SMS-Bridge/FIBER-SIGNER-SPEC.md`.  
UART: 921600 baud, newline-delimited JSON-RPC.

Without signer: kiosk runs in degraded mode (can view channels/invoices, signing disabled).

## Related

- `../CKB-SMS-Bridge/fiber-htlc.js` — Fiber RPC wrapper (reused by bridge)
- `../CKB-SMS-Bridge/FIBER-SIGNER-SPEC.md` — signer hardware spec
- `../wyltek-embedded-builder` — ESP32-P4 signer firmware base
