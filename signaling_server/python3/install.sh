#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing signaling service from $SCRIPT_DIR"

sudo systemctl stop signaling.service
# Replace HOME_WORK with actual path before copying
echo "Updating service file with correct paths..."
sed "s|/home/pi/signaling|$SCRIPT_DIR|g" ./signaling.service | sudo tee /etc/systemd/system/signaling.service > /dev/null

sudo systemctl enable signaling.service
sudo systemctl restart signaling.service
sudo systemctl status signaling.service