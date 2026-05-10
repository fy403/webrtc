#!/bin/bash
# Run script for av_track container with configuration file support

# Default values
IMAGE_NAME="${IMAGE_NAME:-webrtc/av-track:latest}"
CONTAINER_NAME="${CONTAINER_NAME:-webrtc_av_track}"
CONFIG_FILE="${CONFIG_FILE:-config.txt}"
NETWORK_MODE="${NETWORK_MODE:-host}"

# Optional: add additional device mappings
DEVICES=""

# Video device mappings
if [ -e /dev/video0 ]; then
    DEVICES="$DEVICES --device=/dev/video0"
fi

# Audio device mappings for ALSA/PulseAudio
if [ -d /dev/snd ]; then
    DEVICES="$DEVICES --device=/dev/snd"
fi

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --device)
            DEVICES="$DEVICES --device=$2"
            shift 2
            ;;
        --name)
            CONTAINER_NAME="$2"
            shift 2
            ;;
        --network)
            NETWORK_MODE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--config config.txt] [--device /dev/xxx] [--name container_name] [--network mode]"
            exit 1
            ;;
    esac
done

# Check if configuration file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    echo "Please create a config file or specify one with --config option"
    echo "Example: $0 --config /path/to/config.txt"
    exit 1
fi

echo "Using configuration file: $CONFIG_FILE"

# Get absolute path of config file
CONFIG_ABS_PATH=$(realpath "$CONFIG_FILE")

# Get directory of config file for volume mount
CONFIG_DIR=$(dirname "$CONFIG_ABS_PATH")
CONFIG_FILENAME=$(basename "$CONFIG_ABS_PATH")

# Stop existing container
docker rm -f $CONTAINER_NAME >/dev/null 2>&1

# Run container
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  $DEVICES \
  -v "$CONFIG_DIR:/app/config" \
  --network $NETWORK_MODE \
  $IMAGE_NAME \
  ./docker-entrypoint.sh "/app/config/$CONFIG_FILENAME"

# Show running containers
echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
