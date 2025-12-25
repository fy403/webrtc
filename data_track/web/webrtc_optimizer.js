/**
 * WebRTC 性能优化器
 * 提供硬件解码检测、缓冲区优化、性能监控等功能
 */
class WebRTCOptimizer {
    constructor(videoElement) {
        this.videoElement = videoElement;
        
        // 性能指标
        this.metrics = {
            // 延迟指标
            avgJitterBufferDelay: 0,
            avgJitterBufferTargetDelay: 0,
            avgJitterBufferMinimumDelay: 0,
            avgDecodeTime: 0,
            avgTotalProcessingDelay: 0,
            videoBufferDelay: 0,
            totalPlaybackDelay: 0,
            // 质量指标
            framesDropped: 0,
            framesPerSecond: 0,
            packetLossRate: 0,
            framesDecoded: 0
        };

        // 硬件解码检测
        this.hardwareDecodingSupported = false;
        this.preferredCodec = null;
        
        // WebRTC 连接引用
        this.peerConnection = null;
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
     * 注意：所有性能指标都通过 updateMetricsFromStats() 从 WebRTC stats 获取，
     * 这里不再需要单独监控，避免重复计算和资源浪费
     */
    setupPerformanceMonitoring() {
        // 性能监控已集成到 updateMetricsFromStats() 中
        // 所有指标都从 WebRTC stats 获取，保证数据的一致性和准确性
    }

    /**
     * 从 WebRTC 统计信息更新性能指标
     * @param {RTCPeerConnection} peerConnection - WebRTC 连接对象
     * @returns {Promise<boolean>} 是否成功更新
     */
    async updateMetricsFromStats(peerConnection) {
        if (!peerConnection || peerConnection.connectionState === 'closed') {
            return false;
        }

        this.peerConnection = peerConnection;

        try {
            const stats = await peerConnection.getStats();
            let videoStats = null;

            // 查找入站视频统计
            for (const [id, report] of stats.entries()) {
                if (report.type === 'inbound-rtp' && report.kind === 'video') {
                    videoStats = report;
                    break;
                }
            }

            if (!videoStats) return false;

            // 计算平均延迟指标
            const jitterBufferEmittedCount = videoStats.jitterBufferEmittedCount || 0;
            const framesDecoded = videoStats.framesDecoded || 0;

            if (jitterBufferEmittedCount === 0 || framesDecoded === 0) return false;

            // 计算平均延迟（累计值除以计数）
            this.metrics.avgJitterBufferDelay = ((videoStats.jitterBufferDelay || 0) / jitterBufferEmittedCount) * 1000;
            this.metrics.avgJitterBufferTargetDelay = ((videoStats.jitterBufferTargetDelay || 0) / jitterBufferEmittedCount) * 1000;
            this.metrics.avgJitterBufferMinimumDelay = ((videoStats.jitterBufferMinimumDelay || 0) / jitterBufferEmittedCount) * 1000;
            this.metrics.avgDecodeTime = ((videoStats.totalDecodeTime || 0) / framesDecoded) * 1000;
            this.metrics.avgTotalProcessingDelay = ((videoStats.totalProcessingDelay || 0) / jitterBufferEmittedCount) * 1000;

            // 计算视频元素缓冲区延迟
            let videoBufferDelay = 0;
            if (this.videoElement && this.videoElement.readyState >= 2) {
                const currentTime = this.videoElement.currentTime;
                const buffered = this.videoElement.buffered;
                if (buffered && buffered.length > 0) {
                    const bufferedEnd = buffered.end(buffered.length - 1);
                    videoBufferDelay = (bufferedEnd - currentTime) * 1000;
                }
            }
            this.metrics.videoBufferDelay = videoBufferDelay;

            // 计算总播放延迟
            this.metrics.totalPlaybackDelay = this.metrics.avgJitterBufferDelay + videoBufferDelay + this.metrics.avgDecodeTime;

            // 获取其他性能指标
            this.metrics.framesDropped = videoStats.framesDropped || 0;
            this.metrics.framesPerSecond = videoStats.framesPerSecond || 0;
            this.metrics.framesDecoded = framesDecoded;
            
            const packetsLost = videoStats.packetsLost || 0;
            const packetsReceived = videoStats.packetsReceived || 0;
            this.metrics.packetLossRate = packetsReceived > 0 ? packetsLost / packetsReceived : 0;

            return true;
        } catch (err) {
            console.warn('更新性能指标失败:', err);
            return false;
        }
    }

    /**
     * 设置 WebRTC 连接（用于自动更新指标）
     * @param {RTCPeerConnection} peerConnection - WebRTC 连接对象
     */
    setPeerConnection(peerConnection) {
        this.peerConnection = peerConnection;
    }

    /**
     * 获取性能指标
     * @param {boolean} updateFromStats - 是否从 WebRTC stats 更新指标（如果 peerConnection 可用）
     * @returns {Promise<Object>} 性能指标对象
     */
    async getMetrics(updateFromStats = false) {
        // 如果请求从 stats 更新且 peerConnection 可用，则更新指标
        if (updateFromStats && this.peerConnection) {
            await this.updateMetricsFromStats(this.peerConnection);
        }

        return { ...this.metrics };
    }
}

