#!/bin/bash
set -e

echo "1. Killing old processes..."
sudo kill -9 46097 2>/dev/null || true
sudo kill -9 37878 2>/dev/null || true
sudo kill -9 63244 2>/dev/null || true
sleep 2

echo "2. Checking no one is using /dev/video0..."
fuser -k /dev/video0 2>/dev/null || true

echo "3. Removing v4l2loopback module..."
sudo modprobe -r v4l2loopback 2>/dev/null || true
sleep 1

echo "4. Loading v4l2loopback with video0 and video10..."
sudo modprobe v4l2loopback \
    video_nr=0,10 \
    card_label="OBS Camera,WebRTC Camera" \
    exclusive_caps=1,1
sleep 1

echo "5. Fixing permissions..."
sudo chmod 666 /dev/video0 /dev/video10

echo ""
echo "=== Done! ==="
ls -la /dev/video0 /dev/video10
echo ""
v4l2-ctl --list-devices 2>&1 | grep -A2 loopback

echo ""
echo "Now restart OBS and then start receiver."
