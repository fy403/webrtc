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

# Function to read config value (strips comments and whitespace)
get_config() {
    local key=$1
    local default=$2
    grep "^${key}=" "$CONFIG_FILE" 2>/dev/null | head -1 | cut -d'=' -f2- | sed 's/[[:space:]]*#.*//; s/^[[:space:]]*//; s/[[:space:]]*$//' | tr -d '"' | tr -d "'" | grep -v '^$' || echo "$default"
}

# Check binary exists
check_binary() {
    if [ ! -f ./build/webrtc_publisher ]; then
        echo "ERROR: Binary not found at ./build/webrtc_publisher"
        echo "Files in /app/av_track/build:"
        ls -la ./build/ 2>/dev/null || echo "  (build directory empty or missing)"
        exit 1
    fi
    if [ ! -x ./build/webrtc_publisher ]; then
        echo "ERROR: Binary not executable: ./build/webrtc_publisher"
        ls -la ./build/webrtc_publisher
        exit 1
    fi
    echo "Binary found: $(ls -lh ./build/webrtc_publisher | awk '{print $5}')"
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

        # Wait for sensor to stabilize after ISP reconfiguration
        echo "Waiting for sensor to stabilize..."
        sleep 3
    fi

    # Verify binary exists before entering loop
    check_binary

    # Start RTC stream with config file
    while true; do
        echo "$(date): Starting RTC stream..."

        ./build/webrtc_publisher "$CONFIG_FILE"

        exit_code=$?
        echo "$(date): RTC exited with code $exit_code"

        # Read CHECK_INTERVAL from config
        CHECK_INTERVAL=$(get_config "CHECK_INTERVAL" "2")

        # Wait before restart (首次失败多等一会让硬件稳定)
        if [ "$exit_code" -eq 0 ]; then
            echo "$(date): Stream completed normally, waiting ${CHECK_INTERVAL}s before restart..."
        else
            echo "$(date): Stream failed with code $exit_code, waiting ${CHECK_INTERVAL}s before retry..."
        fi
        sleep "$CHECK_INTERVAL"
    done
}

# Handle shutdown signals (only after main() enters the streaming loop)
trap 'echo "$(date): ⚠ Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

echo "$(date): Entrypoint starting..."
# Run main function
main
