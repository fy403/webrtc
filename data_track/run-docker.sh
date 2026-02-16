#!/bin/bash
# Run script for data_track container

# Default values
IMAGE_NAME="${IMAGE_NAME:-webrtc/data-track:latest}"
CONTAINER_NAME="${CONTAINER_NAME:-webrtc_data_track}"
CLIENT_ID="${CLIENT_ID:-data_Dd8fgkoKo90}"
NETWORK_MODE="${NETWORK_MODE:-host}"

# Device configuration
TTY_PORT="/dev/ttyUSB0"
GSM_PORT="/dev/ttyACM0"

# Auto-detect devices
echo "Checking devices..."
DEVICE_ARGS=""

# Check TTY_PORT
if [ -e "$TTY_PORT" ]; then
    DEVICE_ARGS="$DEVICE_ARGS --device=$TTY_PORT"
    echo "Found: $TTY_PORT"
else
    echo "Warning: $TTY_PORT not found"
fi

# Check GSM_PORT
if [ -e "$GSM_PORT" ]; then
    DEVICE_ARGS="$DEVICE_ARGS --device=$GSM_PORT"
    echo "Found: $GSM_PORT"
else
    echo "Warning: $GSM_PORT not found"
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
        --name)
            CONTAINER_NAME="$2"
            shift 2
            ;;
        --client-id)
            CLIENT_ID="$2"
            shift 2
            ;;
        --network)
            NETWORK_MODE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--motor-port /dev/xxx] [--gsm-port /dev/xxx] [--name container_name] [--client-id id] [--network mode]"
            exit 1
            ;;
    esac
shift
done

echo "Starting container..."
if [ -n "$DEVICE_ARGS" ]; then
    echo "Using devices: $DEVICE_ARGS"
else
    echo "No devices attached"
fi

# Run container
docker run -d \
  --name $CONTAINER_NAME \
  --restart unless-stopped \
  $DEVICE_ARGS \
  -e TTY_PORT=$TTY_PORT \
  -e GSM_PORT=$GSM_PORT \
  -e CLIENT_ID=$CLIENT_ID \
  --network $NETWORK_MODE \
  $IMAGE_NAME

echo "Container $CONTAINER_NAME started"
echo "View logs: docker logs -f $CONTAINER_NAME"
