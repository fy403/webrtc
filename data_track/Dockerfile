# Dockerfile for data_track WebRTC control application
FROM alifys/ubuntu:arm64

# Metadata
LABEL maintainer="webrtc-data-track"
LABEL description="WebRTC Data Track control application with motor and GSM support"

# Set working directory
WORKDIR /app/data_track

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
ENV CHECK_INTERVAL=2
ENV CLIENT_ID="data_Dd8fgkoKo90"
ENV STUN_SERVER="stun.l.google.com"
ENV STUN_SERVER_PORT=19302
ENV TURN_SERVER="tx.fy403.cn"
ENV TURN_SERVER_PORT=3478
ENV USER="fy403"
ENV PASSWD="qwertyuiop"
ENV TTY_PORT=/dev/ttyUSB0
ENV TTY_BAUDRATE=115200
ENV GSM_PORT=/dev/ttyACM0
ENV GSM_BAUDRATE=115200
ENV MOTOR_DRIVER_TYPE=uart
ENV FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -f webrtc_publisher || exit 1

# Set entrypoint
ENTRYPOINT ["./docker-entrypoint.sh"]
