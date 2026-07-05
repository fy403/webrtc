#!/bin/bash
# Run script for av_track container - reads configuration from config.txt

set -e

IMAGE_NAME="${IMAGE_NAME:-webrtc/av-track:latest}"
CONTAINER_NAME="webrtc_av_track"
NETWORK_MODE="host"

# Check argument
if [ $# -ne 1 ]; then
    echo "Usage: $0 config.txt"
    echo "Example: $0 /path/to/config.txt"
    exit 1
fi

CONFIG_FILE="$1"

# Check if configuration file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    exit 1
fi

echo "Using configuration file: $CONFIG_FILE"

# Parse devices from config file (strips comments and whitespace)
parse_config() {
    local key="$1"
    local file="$2"
    grep "^${key}=" "$file" 2>/dev/null | head -1 | cut -d'=' -f2- | sed 's/[[:space:]]*#.*//; s/^[[:space:]]*//; s/[[:space:]]*$//'
}

VIDEO_DEVICE=$(parse_config "videoDevice" "$CONFIG_FILE")
AUDIO_DEVICE=$(parse_config "audioDevice" "$CONFIG_FILE")
SPEAKER_DEVICE=$(parse_config "speakerDevice" "$CONFIG_FILE")

# Build device mappings
DEVICES=""

# Rockchip MPP hardware encoder devices (if available)
for dev in /dev/mpp_service /dev/rga /dev/dri/renderD128 /dev/dri/renderD129 /dev/dma_heap/system; do
    if [ -e "$dev" ]; then
        DEVICES="$DEVICES --device=$dev"
        echo "Rockchip device: $dev"
    fi
done

if [ -n "$VIDEO_DEVICE" ] && [ -e "$VIDEO_DEVICE" ]; then
    DEVICES="$DEVICES --device=$VIDEO_DEVICE"
    echo "Video device: $VIDEO_DEVICE"
elif [ -n "$VIDEO_DEVICE" ]; then
    echo "Warning: Video device not found: $VIDEO_DEVICE"
fi

# Audio device (ALSA/PulseAudio)
if [ -n "$AUDIO_DEVICE" ] || [ -n "$SPEAKER_DEVICE" ]; then
    if [ -d /dev/snd ]; then
        DEVICES="$DEVICES --device=/dev/snd"
        echo "Audio devices enabled (/dev/snd)"
    else
        echo "Warning: /dev/snd not found, audio may not work"
    fi
fi

# Get absolute path of config file
CONFIG_ABS_PATH=$(realpath "$CONFIG_FILE")
CONFIG_DIR=$(dirname "$CONFIG_ABS_PATH")
CONFIG_FILENAME=$(basename "$CONFIG_ABS_PATH")

# Stop & remove existing container first (release /dev/video0 before reconfiguring ISP)
echo "Removing old container if exists: $CONTAINER_NAME"
docker stop "$CONTAINER_NAME" >/dev/null 2>&1 || true
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
sleep 1
# 确认旧容器已删除，防止 name conflict
if docker ps -a --format '{{.Names}}' 2>/dev/null | grep -q "^${CONTAINER_NAME}$"; then
    echo "WARNING: Old container still exists, retrying remove..."
    docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
    sleep 1
fi

# Run container
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  $DEVICES \
  -v "$CONFIG_DIR:/app/config" \
  --network $NETWORK_MODE \
  $IMAGE_NAME \
  "/app/config/$CONFIG_FILENAME"

# Show running containers
echo ""
echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
docker logs -f $CONTAINER_NAME