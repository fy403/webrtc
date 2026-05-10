#!/bin/bash
# Entrypoint script for av_track container
set -e

# Configuration file path (can be passed as argument)
CONFIG_FILE="${1:-./config.txt}"

# Function to check if device exists
check_device() {
    local device=$1
    if [ ! -e "$device" ]; then
        echo "ERROR: Device $device does not exist!"
        echo "Available video devices:"
        ls -la /dev/video* 2>/dev/null || echo "  No video devices found"
        exit 1
    fi
    echo "Device $device found"
}

# Function to read config value
get_config() {
    local key=$1
    local default=$2
    grep "^${key}=" "$CONFIG_FILE" 2>/dev/null | cut -d'=' -f2- | tr -d '"' | tr -d "'" || echo "$default"
}

# Main entrypoint
main() {
    echo "$(date): Starting av_track container..."

    # Check if configuration file exists
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "ERROR: Configuration file not found: $CONFIG_FILE"
        exit 1
    fi
    
    echo "Using configuration file: $CONFIG_FILE"

    # Read video device from config
    VIDEO_DEVICE=$(get_config "videoDevice" "")
    
    # Check if video device exists (if not RTSP/UDP stream)
    if [[ "$VIDEO_DEVICE" == /dev/* ]]; then
        check_device "$VIDEO_DEVICE"

        # Wait for device to be ready
        echo "Waiting for device to be ready..."
        sleep 1
    fi

    # Start RTC stream with config file
    while true; do
        echo "$(date): Starting RTC stream..."

        ./build/webrtc_publisher "$CONFIG_FILE"

        exit_code=$?
        echo "$(date): RTC exited with code $exit_code"

        # Read CHECK_INTERVAL from config
        CHECK_INTERVAL=$(get_config "CHECK_INTERVAL" "2")

        # Wait before restart
        if [ "$exit_code" -eq 0 ]; then
            echo "$(date): Stream completed normally, waiting before restart..."
        else
            echo "$(date): Stream failed with code $exit_code, waiting before retry..."
        fi
        sleep "$CHECK_INTERVAL"
    done
}

# Handle shutdown signals
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Run main function
main
