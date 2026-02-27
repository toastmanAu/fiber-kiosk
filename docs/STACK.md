# Fiber Kiosk — Software Stack

## Overview

Standalone touchscreen kiosk running on OPi 3B (RK3566, 800×480 DSI touch).
No browser. Native C app using LVGL 9 + libevdev for touch input.

## Stack

```
┌─────────────────────────────────────────────┐
│              fiber-kiosk (C)                │
│                                             │
│  LVGL 9 ── renders UI to framebuffer       │
│  libevdev ── reads touch events             │
│  libcurl  ── HTTP calls to fiber-bridge     │
│  cJSON    ── JSON parse/build               │
│  pthread  ── background polling thread      │
└──────────────┬──────────────────────────────┘
               │ HTTP (localhost)
┌──────────────▼──────────────────────────────┐
│         fiber-bridge (Node.js)              │
│   fiber-htlc.js + signer UART bridge        │
│   Wraps fnn RPC + ESP32-P4 signer          │
└──────────────┬──────────────────────────────┘
               │ UART /dev/ttyACM0
┌──────────────▼──────────────────────────────┐
│         ESP32-P4 Fiber Signer               │
│   Keys, PIN, MuSig2, HTLC signing          │
└─────────────────────────────────────────────┘
```

## Screens

1. **HOME** — node status, balance, peers
2. **CHANNELS** — list open channels, tap to select
3. **SEND** — amount entry, peer selection, route preview
4. **CONFIRM** — sign request, fee breakdown, PIN prompt
5. **RECEIVE** — generate invoice QR, wait for payment
6. **SIGNER** — signer device status, PIN, lock/unlock

## Build

```bash
# Install deps (Armbian / Debian)
sudo apt install build-essential libevdev-dev libcurl4-openssl-dev

# LVGL is vendored as submodule
git submodule add https://github.com/lvgl/lvgl.git vendor/lvgl

# Build
make

# Run (framebuffer direct)
sudo ./fiber-kiosk --fb /dev/fb0 --touch /dev/input/event0
```

## Config

```ini
# /etc/fiber-kiosk.conf
bridge_url   = http://127.0.0.1:7777
signer_port  = /dev/ttyACM0
display_w    = 800
display_h    = 480
poll_ms      = 3000
```
