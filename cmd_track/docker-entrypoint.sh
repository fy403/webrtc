#!/bin/bash
# Entrypoint script for cmd_track container
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

echo "$(date): Starting cmd_track container..."
echo "Configuration file: $CONFIG_FILE"

# Wait for system to be ready
echo "Waiting for system to be ready..."
sleep 1

# Handle shutdown signals
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Start CmdTrack in loop
while true; do
    echo "$(date): Starting CmdTrack shell server..."

    ./build/cmd_track "$CONFIG_FILE"

    exit_code=$?
    echo "$(date): CmdTrack exited with code $exit_code"

    # Wait before restart
    CHECK_INTERVAL=$(grep -E "^CHECK_INTERVAL=" "$CONFIG_FILE" | cut -d'=' -f2)
    CHECK_INTERVAL=${CHECK_INTERVAL:-2}

    if [ "$exit_code" -eq 0 ]; then
        echo "$(date): Server completed normally, waiting ${CHECK_INTERVAL}s before restart..."
    else
        echo "$(date): Server failed with code $exit_code, waiting ${CHECK_INTERVAL}s before retry..."
    fi
    sleep "$CHECK_INTERVAL"
done
