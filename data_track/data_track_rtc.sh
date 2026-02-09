#!/bin/bash
TARGET_HOST="fy403.cn" 
TARGET_PORT=8000
IP_TYPE=4
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="data_Dd8fgkoKo90"
STUN_SERVER="stun.l.google.com"
STUN_SERVER_PORT=19302
TURN_SERVER="tx.fy403.cn"
TURN_SERVER_PORT=3478
USER="fy403"
PASSWD="qwertyuiop"
TTY_PORT=/dev/ttyUSB0 # 电机驱动板usb端口
TTY_BAUDRATE=115200 # 电机驱动板串口波特率
GSM_PORT=/dev/ttyACM0 # 4g模块usb端口
GSM_BAUDRATE=115200 # 4g模块串口波特率
MOTOR_DRIVER_TYPE=uart # 电机驱动类型: uart, crsf

# Path to font file - adjust according to your system
FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

run_rtc() {
    echo "$(date): Starting RTC stream..."
    
    ./build/webrtc_publisher \
    -s $STUN_SERVER -t $STUN_SERVER_PORT \
    -u $TURN_SERVER -p $TURN_SERVER_PORT -U $USER \
    -P $PASSWD \
    -w $TARGET_HOST -x $TARGET_PORT \
    -I $TTY_PORT -T $TTY_BAUDRATE \
    -g $GSM_PORT -G $GSM_BAUDRATE \
    -M $MOTOR_DRIVER_TYPE \
    -c $CLIENT_ID
    
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