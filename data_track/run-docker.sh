#!/bin/bash
# Run script for data_track container

# Default values
IMAGE_NAME="${IMAGE_NAME:-webrtc/data-track:latest}"
CONTAINER_NAME="${CONTAINER_NAME:-webrtc_data_track}"
NETWORK_MODE="${NETWORK_MODE:-host}"

# Default configuration file
CONFIG_FILE="${CONFIG_FILE:-./config.txt}"

# Device configuration
DEFAULT_TTY_PORT="/dev/ttyUSB0"
DEFAULT_GSM_PORT="/dev/ttyACM0"
DEFAULT_GPS_PORT="/dev/ttyUSB1"

# Auto-detect devices
echo "Checking devices..."
DEVICE_ARGS=""

# Check TTY_PORT
TTY_PORT="$DEFAULT_TTY_PORT"
if [ -e "$TTY_PORT" ]; then
    DEVICE_ARGS="$DEVICE_ARGS --device=$TTY_PORT"
    echo "Found: $TTY_PORT"
else
    echo "Warning: $TTY_PORT not found"
fi

# Check GSM_PORT
GSM_PORT="$DEFAULT_GSM_PORT"
if [ -e "$GSM_PORT" ]; then
    DEVICE_ARGS="$DEVICE_ARGS --device=$GSM_PORT"
    echo "Found: $GSM_PORT"
else
    echo "Warning: $GSM_PORT not found"
fi

# Check GPS_PORT
GPS_PORT="$DEFAULT_GPS_PORT"
if [ -e "$GPS_PORT" ]; then
    DEVICE_ARGS="$DEVICE_ARGS --device=$GPS_PORT"
    echo "Found: $GPS_PORT"
else
    echo "Warning: $GPS_PORT not found"
fi

# Parse command line arguments (for override)
while [[ $# -gt 0 ]]; do
    case $1 in
        --motor-port)
            TTY_PORT="$2"
            shift 2
            ;;
        --gsm-port)
            GSM_PORT="$2"
            shift 2
            ;;
        --gps-port)
            GPS_PORT="$2"
            shift 2
            ;;
        --config)
            CONFIG_FILE="$2"
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
            echo "Usage: $0 [--config config.txt.example] [--motor-port /dev/xxx] [--gsm-port /dev/xxx] [--gps-port /dev/xxx] [--name container_name] [--network mode]"
            exit 1
            ;;
    esac
done

echo "Starting container..."
if [ -n "$DEVICE_ARGS" ]; then
    echo "Using devices: $DEVICE_ARGS"
else
    echo "No devices attached"
fi

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
  $DEVICE_ARGS \
  -v "$CONFIG_DIR:/app/config" \
  --network $NETWORK_MODE \
  $IMAGE_NAME \
  ./docker-entrypoint.sh "/app/config/$CONFIG_FILENAME"

# Show running containers
echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
