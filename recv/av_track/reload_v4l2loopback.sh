#!/bin/bash
# Reload v4l2loopback with video0 + video10
# Run this AFTER closing OBS!

set -e

echo "1. Removing old v4l2loopback module..."
sudo modprobe -r v4l2loopback
sleep 1

echo "2. Reloading with 2 devices: video0 (OBS) + video10 (WebRTC)"
sudo modprobe v4l2loopback \
    video_nr=0,10 \
    card_label="OBS Camera,WebRTC Camera" \
    exclusive_caps=1,1

sleep 1

echo "3. Fixing permissions..."
sudo chmod 666 /dev/video0 /dev/video10

echo ""
echo "=== Done! Devices ready ==="
ls -la /dev/video0 /dev/video10
v4l2-ctl --list-devices 2>&1 | grep -A2 v4l2loopback

echo ""
echo "Now restart OBS (it will use /dev/video0)"
echo "Then start: cd /home/fy403/projects/recv/av_track && ./build/webrtc_receiver --config config.txt"
