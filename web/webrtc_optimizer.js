/**
 * WebRTC 性能优化器
 * 提供硬件解码检测、缓冲区优化、性能监控等功能
 */
class WebRTCOptimizer {
    constructor(videoElement) {
        this.videoElement = videoElement;
        
        // 性能指标
        this.metrics = {
            // 延时指标（后端流水线，由服务端通过 DataChannel 推送）
            backendCapture: 0,
            backendDecode: 0,
            backendFilter: 0,
            backendEncode: 0,
            backendSend: 0,
            backendTotal: 0,  // Capture → Send (服务端)
            // 前端延时指标（从 WebRTC stats 获取）
            avgJitterBufferDelay: 0,
            avgJitterBufferTargetDelay: 0,
            avgJitterBufferMinimumDelay: 0,
            avgDecodeTime: 0,
            avgTotalProcessingDelay: 0,
            videoBufferDelay: 0,
            totalPlaybackDelay: 0,
            // 网络指标
            rtt: 0,
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

        // E2E 统计定时器
        this._e2eInterval = null;
        this._e2eDataChannelReady = false;
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

    // ========== 端到端延时统计增强 ==========

    /**
     * 设置 DataChannel 引用（用于回传前端指标到服务端）
     */
    setDataChannel(dc) {
        this._dataChannel = dc;
        this._e2eDataChannelReady = dc && dc.readyState === 'open';
        
        if (dc) {
            dc.onopen = () => { this._e2eDataChannelReady = true; };
            dc.onclose = () => { this._e2eDataChannelReady = false; };
        }
    }

    /**
     * 启动 E2E 延时统计报告（每10秒打印完整管线延时）
     */
    startE2EReporting() {
        if (this._e2eInterval) return;

        this._e2eInterval = setInterval(async () => {
            try {
                const m = await this.getMetrics(true);
                
                const report = {
                    type: 'e2e_report',
                    timestamp: Date.now(),
                    jitterBuffer: Math.round(m.avgJitterBufferDelay * 10) / 10,
                    decode: Math.round(m.avgDecodeTime * 10) / 10,
                    videoBuffer: Math.round(m.videoBufferDelay * 10) / 10,
                    totalPlayback: Math.round(m.totalPlaybackDelay * 10) / 10,
                    rtt: Math.round(m.rtt * 10) / 10,
                    packetLoss: Math.round(m.packetLossRate * 10000) / 10000,
                    fps: Math.round(m.framesPerSecond * 10) / 10,
                    framesDecoded: m.framesDecoded,
                    framesDropped: m.framesDropped
                };

                if (this._dataChannel && this._e2eDataChannelReady && 
                    this._dataChannel.readyState === 'open') {
                    this._dataChannel.send(JSON.stringify(report));
                }

                // ---------- 提取数据 ----------
                const CL = m.framesPerSecond > 0 ? 1000 / m.framesPerSecond : 33.3;  // 采集延时 = 帧间隔
                const S = {
                    cap: CL,
                    dec: m.backendDecode || 0,
                    flt: m.backendFilter || 0,
                    enc: m.backendEncode || 0,
                    snd: m.backendSend || 0,
                    total: CL + (m.backendDecode||0) + (m.backendFilter||0) + (m.backendEncode||0) + (m.backendSend||0),
                    tmax: CL + (m._beTotalMax||0)
                };
                const R = {
                    jb: m.avgJitterBufferDelay || 0,
                    dec: m.avgDecodeTime || 0,
                    vb: m.videoBufferDelay || 0,
                    total: (m.avgJitterBufferDelay || 0) + (m.avgDecodeTime || 0) + (m.videoBufferDelay || 0)
                };
                const N = m.rtt || 0;
                const totalE2E = S.total + N + R.total;
                const fps = m.framesPerSecond.toFixed(0);
                const loss = (m.packetLossRate * 100).toFixed(1);

                // ---------- 工具函数 ----------
                const fmt = (v) => (v >= 0.05 ? v.toFixed(1) + 'ms' : '  --').padStart(6);
                const color = (v) => v < 5 ? '#4caf50' : v < 15 ? '#ff9800' : '#f44336';
                const emoji = (v) => v < 5 ? '✅' : v < 15 ? '⚡' : '🔴';
                const bold = (s) => `font-weight:bold;${s}`;

                // ---------- 打印主标题 ----------
                console.groupCollapsed(
                    `%c📊 E2E 延时报告 %c│ 总延时 ${fmt(totalE2E)} %c${emoji(totalE2E)}`,
                    'font-weight:bold;color:#00bcd4;font-size:14px',
                    'color:#888;font-size:12px',
                    `color:${color(totalE2E)};font-weight:bold;font-size:16px`
                );

                // ---------- Sender ----------
                console.log(
                    `%c🖥️  Sender  %c│ Capture ${fmt(S.cap)}  Decode ${fmt(S.dec)}  Filter ${fmt(S.flt)}  Encode ${fmt(S.enc)}  Send ${fmt(S.snd)}`,
                    'font-weight:bold;color:#2196f3',
                    'color:#666'
                );
                console.log(
                    `%c           │ 总耗时 ${fmt(S.total)}  %c${emoji(S.total)}  (峰值 ${fmt(S.tmax)})`,
                    'color:#666',
                    `color:${color(S.total)};font-weight:bold`
                );

                // ---------- 分隔线 ----------
                console.log(`%c  ────────────────────────────────────────────────────────`, 'color:#9e9e9e');

                // ---------- Network ----------
                console.log(
                    `%c🌐  Network %c│ RTT ${fmt(N)}  %c${emoji(N)}`,
                    'font-weight:bold;color:#9c27b0',
                    'color:#666',
                    `color:${color(N)};font-weight:bold`
                );

                console.log(`%c  ────────────────────────────────────────────────────────`, 'color:#9e9e9e');

                // ---------- Receiver ----------
                console.log(
                    `%c📺  Receiver%c│ JitterBuf ${fmt(R.jb)}  Decode ${fmt(R.dec)}  VideoBuf ${fmt(R.vb)}`,
                    'font-weight:bold;color:#ff5722',
                    'color:#666'
                );
                console.log(
                    `%c           │ 总耗时 ${fmt(R.total)}  %c${emoji(R.total)}`,
                    'color:#666',
                    `color:${color(R.total)};font-weight:bold`
                );

                // ---------- 底部汇总 ----------
                console.log(`%c  ════════════════════════════════════════════════════════`, 'color:#9e9e9e');
                console.log(
                    `%c🎯 端到端总延时 %c${fmt(totalE2E)}  %c${emoji(totalE2E)}  │  FPS: %c${fps}  %c丢包: ${loss}%%`,
                    'font-weight:bold;font-size:13px;color:#333',
                    `font-weight:bold;font-size:16px;color:${color(totalE2E)}`,
                    `color:${color(totalE2E)};font-size:18px`,
                    'font-weight:bold;color:#4caf50',
                    'color:#ff9800'
                );
                console.log(
                    `%c📈 帧统计  │ 已解码: ${m.framesDecoded}  │  丢弃: ${m.framesDropped}`,
                    'color:#666;font-size:11px'
                );

                console.groupEnd();

                this.updateE2EDisplay(m);

            } catch (err) {
                console.warn('⚠️ E2E report failed:', err);
            }
        }, 5000);

        console.log('✅ [E2E] Latency reporting started (5s interval)');
    }

    /**
     * 停止 E2E 报告
     */
    stopE2EReporting() {
        if (this._e2eInterval) {
            clearInterval(this._e2eInterval);
            this._e2eInterval = null;
            console.log('[E2E] Latency reporting stopped');
        }
    }

    /**
     * 处理从服务端收到的后端流水线延时数据
     */
    onBackendLatency(data) {
        if (!data || typeof data !== 'object') return;

        this.metrics.backendCapture = data.capture || 0;
        this.metrics.backendDecode = data.decode || 0;
        this.metrics.backendFilter = data.filter || 0;
        this.metrics.backendEncode = data.encode || 0;
        this.metrics.backendSend = data.send || 0;
        this.metrics.backendTotal = data.e2e_total || 0;
        // 各阶段 max
        this.metrics._beCapMax = data.capture_max || 0;
        this.metrics._beDecMax = data.decode_max || 0;
        this.metrics._beFltMax = data.filter_max || 0;
        this.metrics._beEncMax = data.encode_max || 0;
        this.metrics._beSndMax = data.send_max || 0;
        this.metrics._beTotalMax = data.total_max || 0;

        this.updateE2EDisplay(this.metrics);
    }

    /**
     * 更新页面上的 E2E pipeline 显示元素
     */
    updateE2EDisplay(m) {
        const el = document.getElementById('e2ePipeline');
        if (!el) return;

        const parts = [];
        let color = '#32CD32';  // 绿色默认

        if (m.backendTotal > 0) {
            parts.push(`Srv:${m.backendTotal.toFixed(0)}ms`);
            if (m.backendTotal > 80) color = '#FFA500';
            if (m.backendTotal > 150) color = '#FF4500';
        }
        if (m.rtt > 0) {
            parts.push(`Net:${m.rtt.toFixed(0)}ms`);
        }
        if (m.jitterBuffer > 0) {
            parts.push(`JB:${m.jitterBuffer.toFixed(0)}ms`);
        }

        if (parts.length > 0) {
            el.textContent = parts.join(' | ');
            el.style.color = color;
        } else {
            el.textContent = '--';
        }
    }
}

