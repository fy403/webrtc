#!/bin/bash
# Configuration parameters
#TARGET_HOST="[ipv6.fy403.cn]"  # Replace with actual IPv6 address
# IP_TYPE=6
TARGET_HOST="fy403.cn" 
IP_TYPE=4
TARGET_PORT=8000
USER="g035d939b93f4d9303ff74e5c5135deb891345ee621b1ac4cde334f062450e4a"
PASSWD="95575f4a4dc4f54f465372dc2b44999e7a61013545fc5a8d1930bc20a981c70e"
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="dataTrack"
STUN_SERVER="stun.l.google.com"
STUN_SERVER_PORT=19302
TURN_SERVER="turn.cloudflare.com"
TURN_SERVER_PORT=3478

# Path to font file - adjust according to your system
FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Check if host is reachable (IPv4/IPv6)
check_host() {
    if ping -${IP_TYPE} -c 3 -W 2 "$TARGET_HOST" > /dev/null 2>&1; then
        echo "$(date): Host $TARGET_HOST is reachable"
        return 0
    else
        echo "$(date): Host $TARGET_HOST is NOT reachable"
        return 1
    fi
}

# Check if port is open
check_port() {
    if nc -${IP_TYPE} -z -w 3 "$TARGET_HOST" "$TARGET_PORT" > /dev/null 2>&1; then
        echo "$(date): Port $TARGET_PORT on $TARGET_HOST is open"
        return 0
    else
        echo "$(date): Port $TARGET_PORT on $TARGET_HOST is NOT open"
        return 1
    fi
}

run_rtc() {
    echo "$(date): Starting RTC stream..."
    
    ./build/webrtc_publisher \
    -s $STUN_SERVER -t $STUN_SERVER_PORT \
    -u $TURN_SERVER -p $TURN_SERVER_PORT -U $USER \
    -P $PASSWD \
    -w $TARGET_HOST -x $TARGET_PORT \
    -c $CLIENT_ID
    
    local exit_code=$?
    echo "$(date): RTC exited with code $exit_code"
    return $exit_code
}

# Main loop
main() {
    echo "$(date): Starting streaming script"
    
    while true; do
        if check_host && check_port; then
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
        else
            echo "$(date): Health checks failed, waiting before retry..."
            sleep $CHECK_INTERVAL
        fi
    done
}

# Handle shutdown signals properly for systemd
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Run main function
main