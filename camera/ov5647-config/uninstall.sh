#!/bin/bash
#
# OV5647 Camera Configuration - Uninstallation Script
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "╔══════════════════════════════════════════════════════╗"
echo "║     OV5647 Camera Configuration - Uninstaller v2.0        ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root (use sudo)${NC}"
    exit 1
fi

echo -e "${GREEN}[1/4]${NC} Stopping service..."
systemctl stop ov5647-config.service 2>/dev/null || true

echo -e "${GREEN}[2/4]${NC} Disabling service..."
systemctl disable ov5647-config.service 2>/dev/null || true

echo -e "${GREEN}[3/4]${NC} Removing files..."
rm -f /usr/local/bin/ov5647-config-tool.sh
rm -f /etc/systemd/system/ov5647-config.service
rm -f /etc/default/ov5647
rm -rf /usr/local/share/doc/ov5647-config

echo -e "${GREEN}[4/4]${NC} Reloading systemd..."
systemctl daemon-reload

echo ""
echo -e "${GREEN}✓ Uninstallation complete!${NC}"
echo ""
