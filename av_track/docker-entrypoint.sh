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

    # IMX419 sensor pipeline configuration (uses IMX219 tool as compatible)
    VIDEO_TYPE=$(get_config "videoType" "")
    if [ "$VIDEO_TYPE" = "imx419" ]; then
        echo "$(date): IMX419 sensor detected, configuring pipeline for 1080p..."
        set +e  # 管线配置失败不应终止容器
        ./imx219-config-tool.sh set 1080p
        CONFIG_EXIT=$?
        set -e
        if [ "$CONFIG_EXIT" -ne 0 ]; then
            echo "$(date): WARNING - IMX419 pipeline config exited with code $CONFIG_EXIT, continuing anyway..."
        else
            echo "$(date): IMX419 pipeline config OK"
        fi
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
