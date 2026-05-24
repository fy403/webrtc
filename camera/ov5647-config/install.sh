#!/bin/bash
#
# OV5647 Camera Configuration - Installation Script
# One-click installation script
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "╔════════════════════════════════════════════════════════╗"
echo "║     OV5647 Camera Configuration - Installer v2.0          ║"
echo "╚════════════════════════════════════════════════════════╝"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root (use sudo)${NC}"
    exit 1
fi

echo -e "${GREEN}[1/5]${NC} Creating directories..."
mkdir -p /usr/local/bin
mkdir -p /etc/default
mkdir -p /etc/systemd/system
mkdir -p /usr/local/share/doc/ov5647-config

echo -e "${GREEN}[2/5]${NC} Installing main script..."
cp "$(dirname "$0")/ov5647-config-tool.sh" /usr/local/bin/ov5647-config-tool.sh
chmod +x /usr/local/bin/ov5647-config-tool.sh

echo -e "${GREEN}[3/5]${NC} Installing configuration file..."
if [ ! -f /etc/default/ov5647 ]; then
    cp "$(dirname "$0")/ov5647.conf" /etc/default/ov5647
else
    echo "  Config file already exists, skipping..."
fi

echo -e "${GREEN}[4/5]${NC} Installing systemd service..."
cp "$(dirname "$0")/ov5647-config.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable ov5647-config.service

echo -e "${GREEN}[5/5]${NC} Installing documentation..."
cp "$(dirname "$0")/README.md" /usr/local/share/doc/ov5647-config/

echo ""
echo -e "${GREEN}✓ Installation complete!${NC}"
echo ""
echo "You can now:"
echo "  - Run: ov5647-config-tool.sh -l"
echo "  - Check: systemctl status ov5647-config.service"
echo "  - View logs: journalctl -u ov5647-config.service -f"
echo ""
echo "Default configuration: 1920x1080 @ 30fps"
echo "To change: edit /etc/default/ov5647"
echo ""
