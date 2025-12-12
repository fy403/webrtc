#!/bin/bash
dos2unix *
# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing av_track_rtc service from $SCRIPT_DIR"

sudo systemctl stop av_track_rtc.service

# Replace HOME_WORK with actual path before copying
echo "Updating service file with correct paths..."
sed "s|/home/pi/av_track|$SCRIPT_DIR|g" ./av_track_rtc.service | sudo tee /etc/systemd/system/av_track_rtc.service > /dev/null

# Enable and start the service
sudo systemctl enable av_track_rtc.service
sudo systemctl start av_track_rtc.service

echo "Service installation completed. Checking status..."
sudo watch -n 1 systemctl status av_track_rtc.service --no-pager -l