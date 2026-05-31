#!/bin/bash
# Run script for cmd_track container - reads configuration from config.txt

set -e

IMAGE_NAME="${IMAGE_NAME:-webrtc/cmd-track:latest}"
CONTAINER_NAME="webrtc_cmd_track"
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

# Get absolute path of config file
CONFIG_ABS_PATH=$(realpath "$CONFIG_FILE")
CONFIG_DIR=$(dirname "$CONFIG_ABS_PATH")
CONFIG_FILENAME=$(basename "$CONFIG_ABS_PATH")

# Stop existing container
docker rm -f $CONTAINER_NAME >/dev/null 2>&1

echo "Mounting host filesystem for shell access..."

# Run container with host filesystem access
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  --privileged \
  -v /:/host:rw \
  -v /dev:/dev:rw \
  -v /proc:/host_proc:ro \
  -v /sys:/host_sys:ro \
  -v "$CONFIG_DIR:/app/config" \
  --network $NETWORK_MODE \
  $IMAGE_NAME \
  "/app/config/$CONFIG_FILENAME"

# Show running containers
echo ""
echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
docker logs -f $CONTAINER_NAME