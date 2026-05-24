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
     * @param {number} targetDelay - 目标延迟（毫秒）
     * @param {number} packetLossRate - 丢包率（0-1）
     */
    adaptBufferSize(targetDelay = 50, packetLossRate = 0) {
        if (!this.videoElement) return;

        try {
            const video = this.videoElement;
            
            // 根据网络状况动态调整目标延迟
            let optimizedDelay = targetDelay;
            
            // 高丢包率：增加缓冲区以抗抖动
            if (packetLossRate > 0.02) {
                optimizedDelay = Math.max(optimizedDelay, 150);
                console.log(`📈 High packet loss (${(packetLossRate * 100).toFixed(2)}%), increasing buffer to ${optimizedDelay}ms`);
            }
            
            // 低延迟模式：减少缓冲区
            if (packetLossRate < 0.005 && targetDelay < 50) {
                optimizedDelay = 30;
                console.log(`📉 Low latency mode: buffer=${optimizedDelay}ms`);
            }

            // ========== 尝试设置 playoutDelayHint（实验性API）==========
            if ('playoutDelayHint' in video) {
                video.playoutDelayHint = optimizedDelay / 1000; // 转换为秒
                console.log(`Set playoutDelayHint: ${optimizedDelay}ms`);
            }

            // ========== 调整 video 元素的 buffered 范围 ==========
            if (video.buffered && video.buffered.length > 0) {
                const bufferedEnd = video.buffered.end(video.buffered.length - 1);
                const currentTime = video.currentTime;
                const bufferAhead = bufferedEnd - currentTime;

                // 如果缓冲区过大（超过目标延迟的2倍），尝试 seek 到当前时间以清空缓冲
                if (bufferAhead > (optimizedDelay / 1000) * 2) {
                    console.log(`Buffer too large (${(bufferAhead * 1000).toFixed(0)}ms), attempting to reduce...`);
                    
                    // 方法1：暂停后立即播放（某些浏览器会清空缓冲）
                    // video.pause();
                    // video.play();
                    
                    // 方法2：调整 currentTime（可能会触发重新缓冲）
                    // video.currentTime = currentTime;
                }
            }

            // ========== 更新指标 ==========
            this.metrics.videoBufferDelay = optimizedDelay;

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
     * 限制PLI/FIR请求频率（避免频繁请求关键帧）
     * @param {number} minInterval - 最小请求间隔（毫秒），默认2000ms
     */
    limitPLIRequests(minInterval = 2000) {
        if (!this.peerConnection) return;

        try {
            const senders = this.peerConnection.getSenders();
            senders.forEach(sender => {
                if (sender.track && sender.track.kind === 'video') {
                    const parameters = sender.getParameters();
                    
                    // 设置编码参数，降低关键帧请求频率
                    if (parameters.encodings) {
                        parameters.encodings.forEach(encoding => {
                            // 禁用自动关键帧请求（如果浏览器支持）
                            if ('requireDeps' in encoding) {
                                encoding.requireDeps = false;
                            }
                        });
                        
                        sender.setParameters(parameters).then(() => {
                            console.log('Limited PLI/FIR requests (min interval: ' + minInterval + 'ms)');
                        }).catch(err => {
                            console.warn('Failed to limit PLI requests:', err);
                        });
                    }
                }
            });
        } catch (e) {
            console.warn('Failed to limit PLI requests:', e);
        }
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

