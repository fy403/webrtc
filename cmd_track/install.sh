#!/bin/bash
dos2unix *
# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing cmd_track_rtc service from $SCRIPT_DIR"

sudo systemctl stop cmd_track_rtc.service

# Replace HOME_WORK with actual path before copying
echo "Updating service file with correct paths..."
sed "s|/home/pi/cmd_track|$SCRIPT_DIR|g" ./cmd_track_rtc.service | sudo tee /etc/systemd/system/cmd_track_rtc.service > /dev/null

# Enable and start the service
sudo systemctl enable cmd_track_rtc.service
sudo systemctl start cmd_track_rtc.service

echo "Service installation completed. Checking status..."
sudo watch -n 1 systemctl status cmd_track_rtc.service --no-pager -l
