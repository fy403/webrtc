#!/bin/bash
TARGET_HOST="fy403.cn" # 信令服务器地址
TARGET_PORT=8000 # 信令服务器端口
IP_TYPE=4 # ipv4 or ipv6
VIDEO_DEVICE="/dev/video1" # 首选摄像头设备
VIDEO_DEVICE_BAK="/dev/video0" # 备选摄像头设备
AUDIO_DEVICE="hw:CARD=Audio,DEV=0" # 音频设备
SAMPLE_RATE=48000 # 音频采样率
CHANNELS=1 # 音频通道数
AUDIO_FORMAT="S16LE" # 音频格式
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="usbcam" # 客户端ID：不填写使用随机值
STUN_SERVER="stun.l.google.com" # STUN服务器地址
STUN_SERVER_PORT=19302 # STUN服务器端口
TURN_SERVER="turn.cloudflare.com" # TURN服务器地址
TURN_SERVER_PORT=3478 # TURN服务器端口
USER="g0xxxxxxxxxxx" # TURN服务器用户名
PASSWD="95yyyyyyyyy" # TURN服务器密码
RESOLUTION="1280x720" # 画面分辨率
FPS=20 # 画面帧率


# Path to font file - adjust according to your system
FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Test if VIDEO_DEVICE works with ffmpeg, if not use VIDEO_DEVICE_BAK
test_video_device() {
    # Automatically detect the first USB camera device
    FIRST_USB_DEVICE=$(v4l2-ctl --list-devices 2>/dev/null | awk '/USB Cam:/,/^$/{if(/\/dev\/video/) {print $1; exit}}')
    
    if [ -n "$FIRST_USB_DEVICE" ]; then
        echo "$(date): Detected first USB camera device: $FIRST_USB_DEVICE"
        VIDEO_DEVICE="$FIRST_USB_DEVICE"
    else
        echo "$(date): No USB camera detected, using default device $VIDEO_DEVICE"
    fi

    echo "$(date): Testing video device $VIDEO_DEVICE..."
    if ffmpeg -f v4l2 -video_size 640x480 -framerate 30 -input_format mjpeg -i "$VIDEO_DEVICE" -vframes 1 -f null - 2>/dev/null; then
        echo "$(date): Primary video device $VIDEO_DEVICE works fine"
        return 0
    else
        echo "$(date): Primary video device $VIDEO_DEVICE failed, testing backup device $VIDEO_DEVICE_BAK..."
        if ffmpeg -f v4l2 -video_size 640x480 -framerate 30 -input_format mjpeg -i "$VIDEO_DEVICE_BAK" -vframes 1 -f null - 2>/dev/null; then
            echo "$(date): Backup video device $VIDEO_DEVICE_BAK works fine"
            # Update VIDEO_DEVICE to use the backup device
            VIDEO_DEVICE="$VIDEO_DEVICE_BAK"
            return 0
        else
            echo "$(date): Both video devices failed"
            return 1
        fi
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
    -c $CLIENT_ID -i $VIDEO_DEVICE \
    -r $RESOLUTION -F $FPS \
    -a $AUDIO_DEVICE \
    -r $SAMPLE_RATE \
    -C $CHANNELS \
    -f $AUDIO_FORMAT

    local exit_code=$?
    echo "$(date): RTC exited with code $exit_code"
    return $exit_code

    -s
stun.l.google.com
-t
19302
-u
turn.cloudflare.com
-p
3478
-U
g0xxxxxxxxxxx
-P
95yyyyyyyyy
-w
fy403.cn
-x
8000
-c
usbcam
-i
/dev/video1
-F
20
-a
"hw:CARD=Audio,DEV=0"
--debug
}

# Main loop
main() {
    echo "$(date): Starting streaming script"
    
    while true; do
        if check_host && check_port; then
            # Test video device before starting stream
            if test_video_device; then
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
                echo "$(date): Video device test failed, waiting before retry..."
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