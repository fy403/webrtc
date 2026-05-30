FROM alifys/ubuntu:arm64

# 1. 卸载原生 ffmpeg 所有包
RUN apt-get update && \
    apt-get remove -y \
        ffmpeg \
        libavcodec-dev libavcodec58 \
        libavdevice-dev libavdevice58 \
        libavfilter-dev libavfilter7 \
        libavformat-dev libavformat58 \
        libavutil-dev libavutil56 \
        libpostproc-dev libpostproc55 \
        libswresample-dev libswresample3 \
        libswscale-dev libswscale5 \
        libx264-dev libx265-dev \
        libffms2-5 && \
    apt-get autoremove -y && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# 2. 清理残留的旧版 ffmpeg 库文件和头文件
RUN rm -f /usr/lib/aarch64-linux-gnu/libavcodec* \
          /usr/lib/aarch64-linux-gnu/libavdevice* \
          /usr/lib/aarch64-linux-gnu/libavfilter* \
          /usr/lib/aarch64-linux-gnu/libavformat* \
          /usr/lib/aarch64-linux-gnu/libavutil* \
          /usr/lib/aarch64-linux-gnu/libpostproc* \
          /usr/lib/aarch64-linux-gnu/libswresample* \
          /usr/lib/aarch64-linux-gnu/libswscale* \
          /usr/lib/aarch64-linux-gnu/libx264* && \
    rm -rf /usr/include/libavcodec \
           /usr/include/libavdevice \
           /usr/include/libavfilter \
           /usr/include/libavformat \
           /usr/include/libavutil \
           /usr/include/libpostproc \
           /usr/include/libswresample \
           /usr/include/libswscale \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libavcodec.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libavdevice.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libavfilter.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libavformat.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libavutil.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libpostproc.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libswresample.pc \
           /usr/lib/aarch64-linux-gnu/pkgconfig/libswscale.pc

# 3. 拷贝 Rockchip FFmpeg 二进制
COPY ffmpeg-files/usr/bin/ffmpeg /usr/bin/
COPY ffmpeg-files/usr/bin/ffprobe /usr/bin/

# 4. 拷贝 Rockchip FFmpeg 共享库和静态库
COPY ffmpeg-files/usr/lib/libavcodec* /usr/lib/
COPY ffmpeg-files/usr/lib/libavdevice* /usr/lib/
COPY ffmpeg-files/usr/lib/libavfilter* /usr/lib/
COPY ffmpeg-files/usr/lib/libavformat* /usr/lib/
COPY ffmpeg-files/usr/lib/libavutil* /usr/lib/
COPY ffmpeg-files/usr/lib/libpostproc* /usr/lib/
COPY ffmpeg-files/usr/lib/libswresample* /usr/lib/
COPY ffmpeg-files/usr/lib/libswscale* /usr/lib/

# 5. 拷贝运行时依赖库 (librga, librockchip_mpp, libx264 v164)
COPY ffmpeg-files/deps/ /usr/lib/aarch64-linux-gnu/

# 6. 拷贝头文件、pkgconfig、数据文件
COPY ffmpeg-files/usr/include/ /usr/include/
COPY ffmpeg-files/usr/lib/pkgconfig/ /usr/lib/pkgconfig/
COPY ffmpeg-files/usr/share/ffmpeg/ /usr/share/ffmpeg/

# 7. 更新动态链接库缓存 (ldconfig 可能把 symlink 变硬拷贝，需修复)
RUN ldconfig && \
    # 恢复 FFmpeg 库的符号链接（ldconfig 可能破坏它们）
    for lib in libavcodec libavdevice libavfilter libavformat libavutil libpostproc libswresample libswscale; do \
        soversion=$(ls /usr/lib/${lib}.so.*.*.* 2>/dev/null | head -1 | rev | cut -d. -f1-2 | rev); \
        sofile=$(ls /usr/lib/${lib}.so.*.*.* 2>/dev/null | head -1 | xargs basename); \
        if [ -n "$sofile" ]; then \
            ln -sf "$sofile" /usr/lib/${lib}.so; \
            ln -sf "$sofile" /usr/lib/${lib}.so.${soversion%%.*}; \
        fi; \
    done && \
    # 修复 libx265 和 liblzma 开发符号链接 (卸载 libx265-dev 后丢失)
    ln -sf /lib/aarch64-linux-gnu/libx265.so.199 /lib/aarch64-linux-gnu/libx265.so && \
    ln -sf /lib/aarch64-linux-gnu/liblzma.so.5.2.5 /lib/aarch64-linux-gnu/liblzma.so

# 9. 验证
RUN ffmpeg -version && ffprobe -version
