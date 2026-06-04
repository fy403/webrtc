#!/bin/bash
# Run script for data_track container - reads configuration from config.txt

set -e

IMAGE_NAME="${IMAGE_NAME:-webrtc/data-track:latest}"
CONTAINER_NAME="webrtc_data_track"
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

# Parse devices from config file (strip inline comments and whitespace)
parse_config() {
    local key="$1"
    local file="$2"
    grep "^${key}=" "$file" 2>/dev/null | cut -d'=' -f2- | cut -d'#' -f1 | xargs
}

USB_DEVICE=$(parse_config "usbDevice" "$CONFIG_FILE")
GSM_PORT=$(parse_config "gsmPort" "$CONFIG_FILE")
GPS_PORT=$(parse_config "gpsPort" "$CONFIG_FILE")

# Build device mappings
DEVICE_ARGS=""
echo "Checking devices from config file..."

if [ -n "$USB_DEVICE" ]; then
    if [ -e "$USB_DEVICE" ]; then
        DEVICE_ARGS="$DEVICE_ARGS --device=$USB_DEVICE"
        echo "Found motor controller: $USB_DEVICE"
    else
        echo "Warning: Motor controller not found: $USB_DEVICE"
    fi
fi

if [ -n "$GSM_PORT" ]; then
    if [ -e "$GSM_PORT" ]; then
        DEVICE_ARGS="$DEVICE_ARGS --device=$GSM_PORT"
        echo "Found GSM module: $GSM_PORT"
    else
        echo "Warning: GSM module not found: $GSM_PORT"
    fi
fi

if [ -n "$GPS_PORT" ]; then
    if [ -e "$GPS_PORT" ]; then
        DEVICE_ARGS="$DEVICE_ARGS --device=$GPS_PORT"
        echo "Found GPS module: $GPS_PORT"
    else
        echo "Warning: GPS module not found: $GPS_PORT"
    fi
fi

# Get absolute path of config file
CONFIG_ABS_PATH=$(realpath "$CONFIG_FILE")
CONFIG_DIR=$(dirname "$CONFIG_ABS_PATH")
CONFIG_FILENAME=$(basename "$CONFIG_ABS_PATH")

# Stop existing container
docker rm -f $CONTAINER_NAME >/dev/null 2>&1

# Run container
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  $DEVICE_ARGS \
  -v "$CONFIG_DIR:/app/config" \
  --network $NETWORK_MODE \
  $IMAGE_NAME \
  "/app/config/$CONFIG_FILENAME"

# Show running containers
echo ""
echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
docker logs -f $CONTAINER_NAME