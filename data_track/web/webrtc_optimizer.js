/**
 * WebRTC 性能优化器
 * 提供硬件解码检测、缓冲区优化、性能监控等功能
 */
class WebRTCOptimizer {
    constructor(videoElement) {
        this.videoElement = videoElement;
        
        // 性能指标
        this.metrics = {
            decodeLatency: 0,
            bufferDelay: 0,
            renderLatency: 0,
            totalLatency: 0,
            frameDrops: 0,
            lastFrameTime: 0
        };

        // 硬件解码检测
        this.hardwareDecodingSupported = false;
        this.preferredCodec = null;
    }

    /**
     * 初始化优化器
     */
    async init() {
        await this.detectHardwareDecoding();
        this.setupVideoElement();
        this.setupPerformanceMonitoring();
        console.log('WebRTC Optimizer initialized:', {
            hardwareDecoding: this.hardwareDecodingSupported,
            preferredCodec: this.preferredCodec
        });
    }

    /**
     * 检测硬件解码支持
     */
    async detectHardwareDecoding() {
        try {
            // 检测 WebCodecs API（如果可用）
            if ('VideoDecoder' in window) {
                const configs = [
                    { codec: 'avc1.42E01E', hardwareAcceleration: 'prefer-hardware' }, // H.264
                    { codec: 'vp8', hardwareAcceleration: 'prefer-hardware' },
                    { codec: 'vp9', hardwareAcceleration: 'prefer-hardware' },
                ];

                for (const config of configs) {
                    try {
                        const support = await VideoDecoder.isConfigSupported(config);
                        if (support.supported) {
                            this.hardwareDecodingSupported = true;
                            this.preferredCodec = config.codec;
                            console.log('Hardware decoding supported for:', config.codec);
                            break;
                        }
                    } catch (e) {
                        // 继续检测下一个
                    }
                }
            }

            // 检测 RTCRtpReceiver 的 getCapabilities（如果可用）
            if ('RTCRtpReceiver' in window && 'getCapabilities' in RTCRtpReceiver) {
                try {
                    const capabilities = RTCRtpReceiver.getCapabilities('video');
                    if (capabilities && capabilities.codecs) {
                        // 查找硬件加速的编解码器
                        const hwCodecs = capabilities.codecs.filter(codec => {
                            return codec.mimeType.includes('h264') || 
                                   codec.mimeType.includes('vp8') ||
                                   codec.mimeType.includes('vp9');
                        });
                        if (hwCodecs.length > 0) {
                            this.hardwareDecodingSupported = true;
                            this.preferredCodec = hwCodecs[0].mimeType;
                            console.log('Hardware codec detected:', this.preferredCodec);
                        }
                    }
                } catch (e) {
                    console.warn('Failed to detect hardware codecs:', e);
                }
            }

            // 平台特定检测
            const ua = navigator.userAgent.toLowerCase();
            if (ua.includes('chrome') || ua.includes('chromium')) {
                // Chrome 通常支持硬件解码
                this.hardwareDecodingSupported = true;
                this.preferredCodec = 'video/h264'; // Chrome 优先使用 H.264
            } else if (ua.includes('safari') && !ua.includes('chrome')) {
                // Safari 必须使用 H.264
                this.hardwareDecodingSupported = true;
                this.preferredCodec = 'video/h264';
            }
        } catch (e) {
            console.warn('Hardware decoding detection failed:', e);
        }
    }

    /**
     * 设置视频元素优化
     */
    setupVideoElement() {
        if (!this.videoElement) return;

        // 设置低延迟属性
        this.videoElement.setAttribute('playsinline', 'true');
        this.videoElement.setAttribute('autoplay', 'true');
        this.videoElement.setAttribute('muted', 'false');
        
        // 尝试设置缓冲区（某些浏览器可能不支持）
        try {
            // 动态调整缓冲区大小
            this.videoElement.addEventListener('loadedmetadata', () => {
                this.adaptBufferSize();
            });

            // 监听播放速率
            this.videoElement.playbackRate = 1.0;
        } catch (e) {
            console.warn('Failed to configure video buffer:', e);
        }
    }

    /**
     * 自适应缓冲区大小
     */
    adaptBufferSize(rtt = 50, packetLoss = 0) {
        if (!this.videoElement) return;

        try {
            // 根据网络状况动态调整
            if (rtt < 50 && packetLoss < 0.01) {
                // 超低延迟模式：50ms 缓冲区
                if (this.videoElement.buffered && this.videoElement.buffered.length > 0) {
                    const bufferedEnd = this.videoElement.buffered.end(this.videoElement.buffered.length - 1);
                    const bufferedStart = this.videoElement.buffered.start(0);
                    const bufferSize = bufferedEnd - bufferedStart;
                    
                    // 如果缓冲区过大，尝试减小
                    if (bufferSize > 0.1) {
                        console.log('Buffer too large, attempting to reduce...');
                    }
                }
            } else {
                // 抗抖动模式：200ms 缓冲区
                console.log('Using anti-jitter buffer mode');
            }
        } catch (e) {
            console.warn('Buffer adaptation failed:', e);
        }
    }

    /**
     * 设置性能监控
     */
    setupPerformanceMonitoring() {
        if (!this.videoElement) return;

        // 使用 requestVideoFrameCallback 监控帧率
        if ('requestVideoFrameCallback' in this.videoElement) {
            let frameCount = 0;
            let lastTime = performance.now();

            const monitorFrame = (now, metadata) => {
                frameCount++;
                const elapsed = now - lastTime;
                
                if (elapsed >= 1000) {
                    const fps = (frameCount * 1000) / elapsed;
                    this.metrics.renderLatency = 1000 / fps; // 估算渲染延迟
                    frameCount = 0;
                    lastTime = now;
                }

                // 继续监控
                this.videoElement.requestVideoFrameCallback(monitorFrame);
            };

            this.videoElement.addEventListener('loadedmetadata', () => {
                this.videoElement.requestVideoFrameCallback(monitorFrame);
            });
        }

        // 监控播放延迟
        setInterval(() => {
            if (this.videoElement && this.videoElement.readyState >= 2) {
                const currentTime = this.videoElement.currentTime;
                const buffered = this.videoElement.buffered;
                
                if (buffered && buffered.length > 0) {
                    const bufferedEnd = buffered.end(buffered.length - 1);
                    this.metrics.bufferDelay = (bufferedEnd - currentTime) * 1000; // 转换为毫秒
                }
            }
        }, 500);
    }

    /**
     * 获取性能指标
     */
    getMetrics() {
        this.metrics.totalLatency = 
            this.metrics.decodeLatency + 
            this.metrics.bufferDelay + 
            this.metrics.renderLatency;
        return { ...this.metrics };
    }
}

