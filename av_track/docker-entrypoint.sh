#!/bin/bash
# Entrypoint script for av_track container
set -e

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

# Function to display configuration
display_config() {
    echo "========================================"
    echo "WebRTC AV Track Configuration"
    echo "========================================"
    echo "Signaling Server: $TARGET_HOST:$TARGET_PORT"
    echo "Client ID: $CLIENT_ID"
    echo "Video Device: $VIDEO_DEVICE"
    echo "Resolution: $RESOLUTION"
    echo "FPS: $FPS"
    echo "Video Codec: $VIDEO_CODEC"
    echo "Input Format: $INPUT_FORMAT"
    echo "STUN Server: $STUN_SERVER:$STUN_SERVER_PORT"
    echo "TURN Server: $TURN_SERVER:$TURN_SERVER_PORT"
    echo "========================================"
}

# Main entrypoint
main() {
    echo "$(date): Starting av_track container..."

    # Check if video device exists (if not RTSP/UDP stream)
    if [[ "$VIDEO_DEVICE" == /dev/* ]]; then
        check_device "$VIDEO_DEVICE"

        # Wait for device to be ready
        echo "Waiting for device to be ready..."
        sleep 1
    fi

    # Display configuration
    display_config

    # Start RTC stream in loop
    while true; do
        echo "$(date): Starting RTC stream..."

        ./build/webrtc_publisher \
            -s "$STUN_SERVER" -t "$STUN_SERVER_PORT" \
            -u "$TURN_SERVER" -p "$TURN_SERVER_PORT" -U "$USER" \
            -P "$PASSWD" \
            -w "$TARGET_HOST" -x "$TARGET_PORT" \
            -R "$RESOLUTION" -F "$FPS" \
            -V "$INPUT_FORMAT" \
            -E "$VIDEO_CODEC" \
            -c "$CLIENT_ID" -i "$VIDEO_DEVICE"

        exit_code=$?
        echo "$(date): RTC exited with code $exit_code"

        # Wait before restart
        if [ "$exit_code" -eq 0 ]; then
            echo "$(date): Stream completed normally, waiting before restart..."
        else
            echo "$(date): Stream failed with code $exit_code, waiting before retry..."
        fi
        sleep "${CHECK_INTERVAL:-2}"
    done
}

# Handle shutdown signals
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Run main function
main
