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

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -f webrtc_publisher || exit 1

# Set entrypoint
ENTRYPOINT ["./docker-entrypoint.sh"]
