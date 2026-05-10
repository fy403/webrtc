#!/bin/bash
# Entrypoint script for data_track container
set -e

# Check for configuration file argument
if [ $# -lt 1 ]; then
    echo "Error: Configuration file path is required"
    echo "Usage: $0 <config_file>"
    echo "Example: $0 /app/config/config.txt"
    exit 1
fi

CONFIG_FILE="$1"

# Check if configuration file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    exit 1
fi

echo "$(date): Starting data_track container..."
echo "Configuration file: $CONFIG_FILE"

# Wait for devices to be ready
echo "Waiting for devices to be ready..."
sleep 1

# Start RTC stream in loop
while true; do
    echo "$(date): Starting RTC stream..."

    ./build/webrtc_publisher "$CONFIG_FILE"

    exit_code=$?
    echo "$(date): RTC exited with code $exit_code"

    # Wait before restart
    CHECK_INTERVAL=$(grep -E "^CHECK_INTERVAL=" "$CONFIG_FILE" | cut -d'=' -f2)
    CHECK_INTERVAL=${CHECK_INTERVAL:-2}

    if [ "$exit_code" -eq 0 ]; then
        echo "$(date): Stream completed normally, waiting ${CHECK_INTERVAL}s before restart..."
    else
        echo "$(date): Stream failed with code $exit_code, waiting ${CHECK_INTERVAL}s before retry..."
    fi
    sleep "$CHECK_INTERVAL"
done

# Handle shutdown signals
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT
