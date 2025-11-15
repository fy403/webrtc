#!/bin/bash
TARGET_HOST="fy403.cn" 
TARGET_PORT=8000
IP_TYPE=4
USER="g0xxxxxxxxxxx"
PASSWD="95yyyyyyyyy"
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="dataTrack"
STUN_SERVER="stun.l.google.com"
STUN_SERVER_PORT=19302
TURN_SERVER="turn.cloudflare.com"
TURN_SERVER_PORT=3478
TTY_PORT=/dev/ttyUSB0 # 电机驱动板usb端口
TTY_BAUDRATE=115200 # 电机驱动板串口波特率
GSM_PORT=/dev/ttyACM0 # 4g模块usb端口
GSM_BAUDRATE=115200 # 4g模块串口波特率

# Path to font file - adjust according to your system
FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Check if tty port exists, if not, use the first available usb interface
check_tty_port() {
    if [ ! -c "$TTY_PORT" ]; then
        echo "$(date): TTY port $TTY_PORT does not exist"
        # Find the first available USB tty device
        local first_usb=$(ls /dev/ttyUSB* 2>/dev/null | head -n 1)
        if [ -n "$first_usb" ] && [ -c "$first_usb" ]; then
            echo "$(date): Using first available USB TTY port: $first_usb"
            TTY_PORT="$first_usb"
        else
            echo "$(date): No USB TTY ports available"
        fi
    else
        echo "$(date): TTY port $TTY_PORT exists"
    fi
}


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
    -I $TTY_PORT -T $TTY_BAUDRATE \
    -g $GSM_PORT -G $GSM_BAUDRATE \
    -c $CLIENT_ID
    
    local exit_code=$?
    echo "$(date): RTC exited with code $exit_code"
    return $exit_code
}

# Main loop
main() {
    echo "$(date): Starting streaming script"
    
    while true; do
        # Check and update TTY port if needed
        check_tty_port
        
#        if check_host && check_port; then
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
#        else
#            echo "$(date): Health checks failed, waiting before retry..."
#            sleep $CHECK_INTERVAL
#        fi
    done
}

# Handle shutdown signals properly for systemd
trap 'echo "$(date): Shutdown signal received, exiting..."; exit 0' SIGTERM SIGINT

# Run main function
main