#!/bin/bash
# setup.sh — Install fiber-kiosk on OPi 3B (Armbian)
# Run as root

set -e

echo "=== fiber-kiosk setup ==="

# Deps
apt-get install -y \
    build-essential \
    libevdev-dev \
    libcurl4-openssl-dev \
    nodejs \
    npm \
    git

# Clone repo (if not already here)
INSTALL_DIR=/home/phill/fiber-kiosk
if [ ! -d "$INSTALL_DIR" ]; then
    git clone https://github.com/toastmanAu/fiber-kiosk "$INSTALL_DIR"
fi
cd "$INSTALL_DIR"

# LVGL submodule
git submodule update --init vendor/lvgl
git submodule update --init vendor/cjson

# Build C kiosk
make -j4

# Install binary
make install

# Bridge deps
cd "$INSTALL_DIR/fiber-bridge"
npm install

# Services
cp "$INSTALL_DIR/scripts/fiber-kiosk.service"  /etc/systemd/system/
cp "$INSTALL_DIR/scripts/fiber-bridge.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable fiber-bridge fiber-kiosk

# Config
if [ ! -f /etc/fiber-kiosk.conf ]; then
cat > /etc/fiber-kiosk.conf << 'EOF'
bridge_url   = http://127.0.0.1:7777
signer_port  = /dev/ttyACM0
fb_device    = /dev/fb0
touch_device = /dev/input/event0
display_w    = 800
display_h    = 480
poll_ms      = 3000
pin_timeout_sec = 300
EOF
fi

# Secrets (not committed)
if [ ! -f /etc/fiber-kiosk.env ]; then
cat > /etc/fiber-kiosk.env << 'EOF'
FIBER_TOKEN=<paste-your-biscuit-token-here>
EOF
chmod 600 /etc/fiber-kiosk.env
fi

# Find touch device
echo ""
echo "=== Touch device detection ==="
ls /dev/input/event* 2>/dev/null || echo "No input devices found"
echo "Run: evtest  (to identify touch device)"
echo "Then update fb_device and touch_device in /etc/fiber-kiosk.conf"

echo ""
echo "=== Done ==="
echo "Start with: systemctl start fiber-bridge fiber-kiosk"
echo "Logs:       journalctl -u fiber-kiosk -f"
