# Dockerfile for av_track WebRTC streaming application
FROM alifys/ubuntu:rockchip

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

# Copy IMX219/IMX419 configuration tool
COPY imx219-config-tool.sh ./

# Build the application
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Make scripts executable
RUN chmod +x docker-entrypoint.sh && \
    chmod +x imx219-config-tool.sh

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -f webrtc_publisher || exit 1

# Set entrypoint with default config path (可被 docker run 参数覆盖)
ENTRYPOINT ["./docker-entrypoint.sh"]
CMD ["./config.txt"]
