#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing data_track_rtc service from $SCRIPT_DIR"

sudo systemctl stop data_track_rtc.service

# Replace HOME_WORK with actual path before copying
echo "Updating service file with correct paths..."
sed "s|/home/pi/data_track|$SCRIPT_DIR|g" ./data_track_rtc.service | sudo tee /etc/systemd/system/data_track_rtc.service > /dev/null

# Enable and start the service
sudo systemctl enable data_track_rtc.service
sudo systemctl start data_track_rtc.service

echo "Service installation completed. Checking status..."
sudo watch -n 1 systemctl status data_track_rtc.service --no-pager -l