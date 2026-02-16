#!/bin/bash
# Entrypoint script for data_track container
set -e

# Function to display configuration
display_config() {
    echo "========================================"
    echo "WebRTC Data Track Configuration"
    echo "========================================"
    echo "Signaling Server: $TARGET_HOST:$TARGET_PORT"
    echo "Client ID: $CLIENT_ID"
    echo "Motor Driver: $MOTOR_DRIVER_TYPE"
    echo "Motor Port: $TTY_PORT ($TTY_BAUDRATE baud)"
    echo "GSM Port: $GSM_PORT ($GSM_BAUDRATE baud)"
    echo "STUN Server: $STUN_SERVER:$STUN_SERVER_PORT"
    echo "TURN Server: $TURN_SERVER:$TURN_SERVER_PORT"
    echo "========================================"
}

# Main entrypoint
main() {
    echo "$(date): Starting data_track container..."

    # Wait for devices to be ready
    echo "Waiting for devices to be ready..."
    sleep 1

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
            -I "$TTY_PORT" -T "$TTY_BAUDRATE" \
            -g "$GSM_PORT" -G "$GSM_BAUDRATE" \
            -M "$MOTOR_DRIVER_TYPE" \
            -c "$CLIENT_ID"

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
