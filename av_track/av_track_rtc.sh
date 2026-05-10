#!/bin/bash
# WebRTC AV Track startup script with configuration file support
# Usage: ./av_track_rtc.sh [config_file]
# If no config file is specified, defaults to config.txt

# Default configuration file
CONFIG_FILE="${1:-./config.txt}"

# Check if configuration file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    echo "Usage: $0 [config_file]"
    echo "Example: $0 config.txt"
    exit 1
fi

echo "Using configuration file: $CONFIG_FILE"

# Read CHECK_INTERVAL from config file
CHECK_INTERVAL=$(grep "^CHECK_INTERVAL=" "$CONFIG_FILE" 2>/dev/null | cut -d'=' -f2- | tr -d '"' | tr -d "'" || echo "2")

# Function to run RTC stream
run_rtc() {
    echo "$(date): Starting RTC stream..."
    
    ./build/webrtc_publisher "$CONFIG_FILE"
    
    local exit_code=$?
    echo "$(date): RTC exited with code $exit_code"
    return $exit_code
}

# Main loop
main() {
    echo "$(date): Starting streaming script"
    
    while true; do
        echo "$(date): All checks passed, starting stream..."
        run_rtc
        
        # If RTC exits normally or with error, wait before restart
        if [ $? -eq 0 ]; then
            echo "$(date): Stream completed normally, waiting before restart..."
            sleep $CHECK_INTERVAL
        else
            echo "$(date): Stream failed, waiting before retry..."
            sleep $CHECK_INTERVAL
        fi
    done
}

# Handle shutdown signals properly for systemd
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Run main function
main
