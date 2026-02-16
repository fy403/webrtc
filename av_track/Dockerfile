# Dockerfile for av_track WebRTC streaming application
FROM alifys/ubuntu:arm64

# Metadata
LABEL maintainer="webrtc-av-track"
LABEL description="WebRTC Audio/Video Track streaming application"

# Set working directory
WORKDIR /app/av_track

# Copy source files
COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt ./

# Copy entrypoint script
COPY docker-entrypoint.sh ./

# Build the application
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Make entrypoint executable
RUN chmod +x docker-entrypoint.sh

# Environment variables with defaults (can be overridden at runtime)
ENV TARGET_HOST="fy403.cn"
ENV TARGET_PORT=8000
ENV IP_TYPE=4
ENV VIDEO_DEVICE="/dev/video1"
ENV AUDIO_FORMAT="alsa"
ENV CHECK_INTERVAL=2
ENV CLIENT_ID="cam_dYFh3H3kf"
ENV STUN_SERVER="stun.l.google.com"
ENV STUN_SERVER_PORT=19302
ENV TURN_SERVER="tx.fy403.cn"
ENV TURN_SERVER_PORT=3478
ENV USER="fy403"
ENV PASSWD="qwertyuiop"
ENV RESOLUTION="640x480"
ENV INPUT_FORMAT="yuyv422"
ENV FPS=30
ENV VIDEO_CODEC="h264"
ENV FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -f webrtc_publisher || exit 1

# Set entrypoint
ENTRYPOINT ["./docker-entrypoint.sh"]
