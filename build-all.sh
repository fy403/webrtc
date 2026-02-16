#!/bin/bash
# Build script for all WebRTC containers

set -e

echo "========================================"
echo "Building all WebRTC containers..."
echo "========================================"

# Build av_track
echo ""
echo "Building av_track..."
cd "$(dirname "$0")/av_track"
docker build -t webrtc/av-track:latest .
echo "av_track built successfully"

# Build data_track
echo ""
echo "Building data_track..."
cd "../data_track"
docker build -t webrtc/data-track:latest .
echo "data_track built successfully"

echo ""
echo "========================================"
echo "All containers built successfully!"
echo "========================================"
echo ""
echo "Available images:"
docker images | grep webrtc
