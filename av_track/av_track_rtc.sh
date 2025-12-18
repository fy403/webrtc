#!/bin/bash
TARGET_HOST="fy403.cn" # 信令服务器地址
TARGET_PORT=8000 # 信令服务器端口
IP_TYPE=4 # ipv4 or ipv6
VIDEO_DEVICE="/dev/video1" # 首选摄像头设备
VIDEO_DEVICE_BAK="/dev/video0" # 备选摄像头设备
#AUDIO_DEVICE="hw:CARD=Audio,DEV=0" # 音频设备
#SAMPLE_RATE=48000 # 音频采样率
#CHANNELS=1 # 音频通道数
AUDIO_FORMAT="alsa" # 音频输入格式
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="cam_dYFh3H3kf" # 客户端ID：不填写使用随机值
STUN_SERVER="stun.l.google.com" # STUN服务器地址
STUN_SERVER_PORT=19302 # STUN服务器端口
TURN_SERVER="tx.fy403.cn"
TURN_SERVER_PORT=3478
USER="fy403"
PASSWD="qwertyuiop"
RESOLUTION="640x480" # 画面分辨率
#INPUT_FORMAT="yuyv422" # mjpeg, yuyv422
#FPS=60 # 画面帧率
#RESOLUTION="1280x720" # 画面分辨率
INPUT_FORMAT="yuyv422" # mjpeg, yuyv422
FPS=30 # 画面帧率
#ADUIO_PLAYER_DEVICE="USB2.0 Device, USB Audio" # 音频播放设备
#ADUIO_PLAYER_SAMPLE_RATE=48000 # 音频采样率
#AUDIO_PLAYER_CHANNELS=2 # 音频通道数
#AUDIO_PLAYER_VOLUME=1 # 音频播放音量

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
    -R $RESOLUTION -F $FPS \
    -V $INPUT_FORMAT \
    -c $CLIENT_ID -i $VIDEO_DEVICE
#    -a $AUDIO_DEVICE \
#    -r $SAMPLE_RATE \
#    -C $CHANNELS \
#    -f $AUDIO_FORMAT
#    -S $ADUIO_PLAYER_DEVICE
#    -O $ADUIO_PLAYER_SAMPLE_RATE
#    -H $AUDIO_PLAYER_CHANNELS
#    -v $AUDIO_PLAYER_VOLUME


    local exit_code=$?
    echo "$(date): RTC exited with code $exit_code"
    return $exit_code
}

# Main loop
main() {
    echo "$(date): Starting streaming script"
    
    while true; do
#        if check_host && check_port; then
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