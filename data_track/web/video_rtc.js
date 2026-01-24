window.addEventListener('load', () => {

    const localId = randomId(4);
    const url = `ws://fy403.cn:8000/${localId}`;

    // ========== 优化配置：低延迟 WebRTC ==========
    const config = {
        iceServers: [{
                urls: ['stun:stun.l.google.com:19302']
            },
            {
                urls: ['turn:tx.fy403.cn:3478?transport=udp'],
                username: 'fy403',
                credential: 'qwertyuiop'
            },
        ],
        // 低延迟优化配置
        bundlePolicy: 'max-bundle',
        rtcpMuxPolicy: 'require',
        // 启用低延迟模式（实验性API，如果支持）
        encodedInsertableStreams: false, // 某些浏览器可能不支持
    };

    // Add localStream variable to store the audio stream
    let localStream;
    const peerConnectionMap = {};
    const dataChannelMap = {};
    let reconnectInterval = null; // PeerConnection 自动重连定时器
    let wsReconnectInterval = null; // WebSocket 重连定时器
    let isReconnecting = false; // 全局标记：是否正在重连中（用于避免并发连接）

    const offerId = document.getElementById('offerId');
    const offerBtn = document.getElementById('offerBtn');
    const _localId = document.getElementById('localId');
    const remoteVideo = document.getElementById('remoteVideo');
    const statusDiv = document.getElementById('status');
    _localId.textContent = localId;

    // 视频配置元素
    const resolutionSelect = document.getElementById('resolutionSelect');
    const resolutionCustom = document.getElementById('resolutionCustom');
    const fpsSelect = document.getElementById('fpsSelect');
    const fpsCustom = document.getElementById('fpsCustom');
    const bitrateSelect = document.getElementById('bitrateSelect');
    const bitrateCustom = document.getElementById('bitrateCustom');
    const formatSelect = document.getElementById('formatSelect');
    const formatCustom = document.getElementById('formatCustom');
    const applyVideoConfig = document.getElementById('applyVideoConfig');

    // 处理自定义输入的启用/禁用
    resolutionSelect.addEventListener('change', () => {
        resolutionCustom.disabled = resolutionSelect.value !== 'custom';
        if (resolutionSelect.value !== 'custom') {
            resolutionCustom.value = '';
        }
    });

    fpsSelect.addEventListener('change', () => {
        fpsCustom.disabled = fpsSelect.value !== 'custom';
        if (fpsSelect.value !== 'custom') {
            fpsCustom.value = '';
        }
    });

    bitrateSelect.addEventListener('change', () => {
        bitrateCustom.disabled = bitrateSelect.value !== 'custom';
        if (bitrateSelect.value !== 'custom') {
            bitrateCustom.value = '';
        }
    });

    formatSelect.addEventListener('change', () => {
        formatCustom.disabled = formatSelect.value !== 'custom';
        if (formatSelect.value !== 'custom') {
            formatCustom.value = '';
        }
    });

    // 发送视频配置
    applyVideoConfig.addEventListener('click', () => {
        // 获取当前活跃的 DataChannel
        const activeIds = Object.keys(dataChannelMap);
        if (activeIds.length === 0) {
            updateStatus('No data channel connected');
            return;
        }

        const dc = dataChannelMap[activeIds[0]];
        if (!dc || dc.readyState !== 'open') {
            updateStatus('Data channel not ready');
            return;
        }

        // 获取配置值
        let resolution = resolutionSelect.value === 'custom' ? resolutionCustom.value : resolutionSelect.value;
        let fps = fpsSelect.value === 'custom' ? parseInt(fpsCustom.value) : parseInt(fpsSelect.value);
        let bitrate = bitrateSelect.value === 'custom' ? parseInt(bitrateCustom.value) : parseInt(bitrateSelect.value);
        let format = formatSelect.value === 'custom' ? formatCustom.value : formatSelect.value;

        // 验证自定义值
        if (!resolution || !resolution.match(/^\d+x\d+$/)) {
            updateStatus('Invalid resolution format (use WxH)');
            return;
        }
        if (isNaN(fps) || fps < 1 || fps > 120) {
            updateStatus('Invalid FPS (1-120)');
            return;
        }
        if (isNaN(bitrate) || bitrate < 100000) {
            updateStatus('Invalid bitrate (min 100000)');
            return;
        }
        if (!format || format.length === 0) {
            updateStatus('Invalid video format');
            return;
        }

        // 发送配置消息
        const configMsg = {
            type: 'video_config',
            resolution: resolution,
            fps: fps,
            bitrate: bitrate,
            format: format
        };

        try {
            dc.send(JSON.stringify(configMsg));
            updateStatus(`Video config sent: ${resolution}, ${fps}fps, ${bitrate}bps, ${format}`);
            console.log('Sent video config:', configMsg);
        } catch (e) {
            updateStatus('Failed to send video config: ' + e.message);
            console.error('Error sending video config:', e);
        }
    });

    // Track status elements
    const videoTrackStatus = document.getElementById('videoTrackStatus');
    const audioTrackStatus = document.getElementById('audioTrackStatus');

    // Function to update track status indicators
    function updateTrackStatus() {
        if (!remoteVideo.srcObject) {
            // No stream - both tracks disabled
            updateStatusIcon(videoTrackStatus, false);
            updateStatusIcon(audioTrackStatus, false);
            return;
        }

        const tracks = remoteVideo.srcObject.getTracks();
        const videoTracks = tracks.filter(t => t.kind === 'video');
        const audioTracks = tracks.filter(t => t.kind === 'audio');

        // Update video track status
        const videoEnabled = videoTracks.length > 0 && videoTracks[0].enabled;
        updateStatusIcon(videoTrackStatus, videoEnabled);

        // Update audio track status
        const audioEnabled = audioTracks.length > 0 && audioTracks[0].enabled;
        updateStatusIcon(audioTrackStatus, audioEnabled);
    }

    // Function to update a single status icon
    function updateStatusIcon(element, enabled) {
        if (!element) return;
        if (enabled) {
            element.classList.add('active');
            element.classList.remove('inactive');
        } else {
            element.classList.remove('active');
            element.classList.add('inactive');
        }
    }

    // Initialize track status
    updateTrackStatus();

    // ========== 性能优化模块 ==========
    // 使用独立的优化类
    const PerformanceOptimizer = new WebRTCOptimizer(remoteVideo);

    // 初始化性能优化器
    PerformanceOptimizer.init();

    // Initialize with "No Signal" overlay visible
    toggleNoSignalOverlay(true);

    // Fullscreen functionality - handle F key press
    function toggleFullscreen() {
        // Use video-with-overlay to include all status overlays in fullscreen
        const videoWithOverlay = document.querySelector('.video-with-overlay');
        if (!videoWithOverlay) {
            console.warn('Video with overlay container not found');
            return;
        }

        // Check if already in fullscreen (handle different browser prefixes)
        const isFullscreen = document.fullscreenElement ||
            document.webkitFullscreenElement ||
            document.mozFullScreenElement ||
            document.msFullscreenElement;

        if (isFullscreen) {
            // Exit fullscreen
            if (document.exitFullscreen) {
                document.exitFullscreen();
            } else if (document.webkitExitFullscreen) {
                document.webkitExitFullscreen();
            } else if (document.mozCancelFullScreen) {
                document.mozCancelFullScreen();
            } else if (document.msExitFullscreen) {
                document.msExitFullscreen();
            }
            console.log('Exited fullscreen');
            updateStatus('Exited fullscreen');
        } else {
            // Enter fullscreen
            if (videoWithOverlay.requestFullscreen) {
                videoWithOverlay.requestFullscreen();
            } else if (videoWithOverlay.webkitRequestFullscreen) {
                videoWithOverlay.webkitRequestFullscreen();
            } else if (videoWithOverlay.mozRequestFullScreen) {
                videoWithOverlay.mozRequestFullScreen();
            } else if (videoWithOverlay.msRequestFullscreen) {
                videoWithOverlay.msRequestFullscreen();
            }
            console.log('Entered fullscreen');
            updateStatus('Entered fullscreen');
        }
    }

    // Add keyboard event listener for F key
    window.addEventListener('keydown', (e) => {
        // Check if F key is pressed (keyCode 70 or key 'f' or 'F')
        if (e.key === 'f' || e.key === 'F' || e.keyCode === 70) {
            // Don't trigger if user is typing in an input field
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.isContentEditable) {
                return;
            }
            e.preventDefault();
            toggleFullscreen();
        }
    });

    // Get local audio stream
    async function getLocalStream() {
        try {
            localStream = await navigator.mediaDevices.getUserMedia({
                audio: true,
                video: true,
            });
            console.log('获取本地音频流成功');
        } catch (error) {
            console.error('获取本地音频流失败:', error);
        }
    }

    console.log('Connecting to signaling...');
    openSignaling(url)
        .then(async (ws) => {
            console.log('WebSocket connected, signaling ready');
            updateStatus('Signaling connected');
            stopWsReconnect(); // WebSocket 连接成功，停止重连

            // Get local audio stream before enabling connections
            await getLocalStream();

            offerId.disabled = false;
            offerBtn.disabled = false;
            offerBtn.onclick = () => offerPeerConnection(ws, offerId.value);
            if (offerId.value) {
                offerBtn.click();
            }
        })
        .catch((err) => {
            console.error(err);
            updateStatus('Signaling connection failed: ' + err.message);
            // 连接失败时也启动重连
            startWsReconnect(url);
        });

    function updateStatus(message) {
        statusDiv.textContent = 'Status: ' + message;
        console.log('Status: ' + message);
    }

    // Helper function to show/hide the "No Signal" overlay
    function toggleNoSignalOverlay(show) {
        const overlay = document.getElementById('noSignalOverlay');
        if (overlay) {
            overlay.style.display = show ? 'flex' : 'none';
        }
    }

    // Helper function to show play button when autoplay fails
    function showPlayButton() {
        // Check if play button already exists
        let playButton = document.getElementById('playButton');
        if (playButton) {
            playButton.style.display = 'block';
            return;
        }

        // Create play button
        playButton = document.createElement('button');
        playButton.id = 'playButton';
        playButton.textContent = '点击播放';
        playButton.style.position = 'absolute';
        playButton.style.top = '50%';
        playButton.style.left = '50%';
        playButton.style.transform = 'translate(-50%, -50%)';
        playButton.style.zIndex = '20';
        playButton.style.padding = '12px 24px';
        playButton.style.backgroundColor = '#4f46e5';
        playButton.style.color = 'white';
        playButton.style.border = 'none';
        playButton.style.borderRadius = '8px';
        playButton.style.fontSize = '16px';
        playButton.style.cursor = 'pointer';
        playButton.style.boxShadow = '0 4px 6px rgba(0, 0, 0, 0.1)';

        playButton.onclick = function () {
            const video = document.getElementById('remoteVideo');
            if (video && video.srcObject) {
                video.play().then(() => {
                    console.log('Media playback started by user interaction');
                    updateStatus('Media playing');
                    playButton.style.display = 'none';
                    toggleNoSignalOverlay(false);
                }).catch(err => {
                    console.error('Error playing media:', err);
                    updateStatus('Media play failed: ' + err.message);
                });
            }
        };

        // Add button to video container
        const videoContainer = document.querySelector('.video-container');
        if (videoContainer) {
            videoContainer.appendChild(playButton);
        }
    }

    function openSignaling(url) {
        return new Promise((resolve, reject) => {
            const ws = new WebSocket(url);
            ws.onopen = () => {
                console.log('Signaling WebSocket connected');
                resolve(ws);
            };
            ws.onerror = () => reject(new Error('WebSocket error'));
            ws.onclose = () => {
                console.error('Signaling WebSocket disconnected');
                updateStatus('Signaling disconnected');
                // 启动 WebSocket 自动重连
                startWsReconnect(url);
            };
            ws.onmessage = (e) => {
                if (typeof (e.data) != 'string')
                    return;
                const message = JSON.parse(e.data);
                console.log('Received signaling message:', message);
                const {
                    id,
                    type
                } = message;

                let pc = peerConnectionMap[id];
                if (!pc) {
                    if (type != 'offer')
                        return;

                    // Create PeerConnection for answer
                    console.log(`Answering to ${id}`);
                    updateStatus(`Incoming call from ${id}`);
                    pc = createPeerConnection(ws, id);
                }

                switch (type) {
                    case 'offer':
                    case 'answer':
                        console.log(`Setting remote ${type} from ${id}`);
                        pc.setRemoteDescription(new RTCSessionDescription({
                            sdp: message.description,
                            type: message.type,
                        })).then(() => {
                            console.log(`Set remote ${type} successfully, connectionState: ${pc.connectionState}, iceState: ${pc.iceConnectionState}`);
                            // 收到 answer 后，重置重连状态
                            if (type == 'answer') {
                                isReconnecting = false;
                            }
                            if (type == 'offer') {
                                // Send answer
                                updateStatus(`Creating answer for ${id}`);
                                sendLocalDescription(ws, id, pc, 'answer');
                            }
                        }).catch(err => {
                            console.error(`Error setting remote ${type}:`, err);
                            updateStatus(`Error setting remote ${type}: ${err.message}`);
                        });
                        break;

                    case 'candidate':
                        console.log(`Adding ICE candidate from ${id}`);
                        pc.addIceCandidate(new RTCIceCandidate({
                            candidate: message.candidate,
                            sdpMid: message.mid,
                            sdpMLineIndex: message.sdpMLineIndex
                        })).then(() => {
                            console.log(`Added ICE candidate successfully, iceState: ${pc.iceConnectionState}`);
                        }).catch(err => {
                            console.error('Error adding ICE candidate:', err);
                        });
                        break;
                }
            }
        });
    }

    function offerPeerConnection(ws, id) {
        if (!id) {
            alert('Please enter a remote ID');
            return;
        }

        // Create PeerConnection
        console.log(`Offering to ${id}`);
        updateStatus(`Offering to ${id}`);
        const pc = createPeerConnection(ws, id);

        // Add only audio tracks to PeerConnection (even though we captured both)
        if (localStream) {
            localStream.getAudioTracks().forEach(track => pc.addTrack(track, localStream));
            console.log('Added local audio tracks to PeerConnection');
        }

        // Create DataChannel
        const label = "test";
        console.log(`Creating DataChannel with label "${label}"`);
        const dc = pc.createDataChannel(label);
        // 保存 ws 引用到 data channel，用于错误处理时的重连
        dc._ws = ws;
        setupDataChannel(dc, id);

        // 添加超时检测：如果5秒内没有收到任何 ICE 候选或 answer，认为对端可能不在线
        pc._connectionTimeout = setTimeout(() => {
            const currentPc = peerConnectionMap[id];
            if (currentPc === pc) {
                const state = pc.connectionState;
                const iceState = pc.iceConnectionState;

                // 如果还在 new 状态，说明对端没有响应，强制关闭并允许重连
                if (state === 'new') {
                    console.warn(`Connection timeout for ${id}, state: ${state}/${iceState}, forcing close`);
                    updateStatus(`Connection timeout to ${id} (no response)`);
                    try {
                        pc.close();
                    } catch (e) {
                        console.warn('Error closing timed-out peer connection:', e);
                    }
                    if (peerConnectionMap[id] === pc) {
                        delete peerConnectionMap[id];
                    }
                    if (dataChannelMap[id]) {
                        delete dataChannelMap[id];
                    }
                    isReconnecting = false; // 重置状态，允许重连
                }
            }
        }, 5000); // 5秒超时

        // Send offer
        updateStatus(`Creating offer for ${id}`);
        sendLocalDescription(ws, id, pc, 'offer');
    }

    // Create and setup a PeerConnection
    function createPeerConnection(ws, id) {
        const pc = new RTCPeerConnection(config);

        // ========== 低延迟优化：配置接收端参数 ==========
        // 在轨道接收后配置低延迟参数
        const configureLowLatency = (transceiver) => {
            if (!transceiver || !transceiver.receiver) return;

            try {
                const receiver = transceiver.receiver;

                // 检查 getParameters 方法是否存在
                if (typeof receiver.getParameters !== 'function') {
                    // 静默返回，依赖 SDP 优化
                    return;
                }

                const parameters = receiver.getParameters();

                console.log('=======parameters:', parameters);

                if (!parameters) {
                    // 静默返回，依赖 SDP 优化
                    return;
                }

                // 检查 setParameters 方法是否存在
                if (typeof receiver.setParameters !== 'function') {
                    // 如果 setParameters 不可用，我们依赖 SDP 优化（这是主要优化方式）
                    // 静默处理，因为 SDP 优化已经足够
                    return;
                }

                // 设置降级偏好为保持帧率（优先降低分辨率而非帧率）
                if (parameters.degradationPreference !== undefined) {
                    parameters.degradationPreference = 'maintain-framerate';
                }

                // 配置编码参数（如果是发送端）
                if (parameters.encodings) {
                    parameters.encodings.forEach(encoding => {
                        // 设置最大比特率（可根据网络状况调整）
                        // if (encoding.maxBitrate === undefined) {
                        //     encoding.maxBitrate = 2000000; // 2 Mbps
                        // }
                    });
                }

                // ========== 关键优化：降低抖动缓冲区延迟 ==========
                // 设置最小播放延迟（目标：30-50ms，而不是默认的 200ms+）
                // 注意：这些属性可能在某些浏览器中不可用
                let hasDelayParams = false;
                if (parameters.minPlayoutDelay !== undefined) {
                    parameters.minPlayoutDelay = 0.03; // 30ms（秒为单位）
                    hasDelayParams = true;
                    console.log('Setting minPlayoutDelay to 30ms');
                }

                // 设置最大播放延迟
                if (parameters.maxPlayoutDelay !== undefined) {
                    parameters.maxPlayoutDelay = 0.1; // 100ms（秒为单位）
                    hasDelayParams = true;
                    console.log('Setting maxPlayoutDelay to 100ms');
                }

                // 只有在有可修改的参数时才调用 setParameters
                const hasModifications = parameters.degradationPreference !== undefined ||
                    hasDelayParams ||
                    (parameters.encodings && parameters.encodings.length > 0);

                if (hasModifications) {
                    receiver.setParameters(parameters).then(() => {
                        // 只在成功时输出日志（可选，用于调试）
                        console.log('Low latency parameters configured for receiver', {
                            minPlayoutDelay: parameters.minPlayoutDelay,
                            maxPlayoutDelay: parameters.maxPlayoutDelay,
                            degradationPreference: parameters.degradationPreference
                        });
                    }).catch(err => {
                        // 静默处理错误，SDP 优化仍然有效
                        console.warn('Failed to set receiver parameters:', err);
                    });
                }
                // 如果没有可修改的参数，静默返回（SDP 优化仍然有效）
            } catch (e) {
                // 静默处理错误，避免过多日志
                if (e.name !== 'TypeError' || !e.message.includes('setParameters')) {
                    console.warn('Failed to configure low latency:', e);
                }
            }
        };

        pc.oniceconnectionstatechange = () => {
            console.log(`ICE Connection state: ${pc.iceConnectionState}`);
            updateStatus(`ICE: ${pc.iceConnectionState}`);

            if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
                console.log(`ICE connection established with ${id}`);
                isReconnecting = false; // ICE 连接成功，重置状态
            } else if (pc.iceConnectionState === 'failed') {
                console.error(`ICE connection failed with ${id}, immediate reconnection`);
                updateStatus(`ICE failed, reconnecting...`);
                isReconnecting = false; // 重置连接状态，允许重连
                toggleNoSignalOverlay(true);
                // ICE 连接失败时立即重连
                if (!reconnectInterval) {
                    startAutoReconnect(ws, id);
                }
            } else if (pc.iceConnectionState === 'disconnected') {
                console.warn(`ICE connection disconnected with ${id}`);
                // ICE disconnected 快速检查（1秒后）
                setTimeout(() => {
                    if (pc.iceConnectionState === 'disconnected' && !reconnectInterval) {
                        console.log(`ICE still disconnected after 1s, reconnecting`);
                        isReconnecting = false; // 重置连接状态，允许重连
                        startAutoReconnect(ws, id);
                    }
                }, 1000);
            }
        };

        pc.onconnectionstatechange = () => {
            console.log(`Connection state: ${pc.connectionState}`);
            updateStatus(`Connection: ${pc.connectionState}`);

            // 清理超时定时器
            if (pc._connectionTimeout) {
                clearTimeout(pc._connectionTimeout);
                pc._connectionTimeout = null;
            }

            if (pc.connectionState === 'connected') {
                updateStatus(`Connected to ${id}`);
                isReconnecting = false; // 重置连接状态
                stopAutoReconnect(); // 连接成功，停止自动重连
            } else if (pc.connectionState === 'failed') {
                console.error(`Connection failed with ${id}, immediate reconnection`);
                updateStatus(`Connection failed with ${id}`);
                isReconnecting = false; // 重置连接状态，允许重连
                toggleNoSignalOverlay(true);
                // 立即重连，不延迟
                startAutoReconnect(ws, id);
            } else if (pc.connectionState === 'disconnected') {
                console.warn(`Connection disconnected with ${id}`);
                updateStatus(`Connection disconnected with ${id}`);
                // disconnected 状态快速检查（1秒后）
                setTimeout(() => {
                    if (pc.connectionState === 'disconnected' && !reconnectInterval) {
                        console.log(`Still disconnected after 1s, reconnecting`);
                        isReconnecting = false; // 重置连接状态，允许重连
                        startAutoReconnect(ws, id);
                    }
                }, 1000);
            } else if (pc.connectionState === 'closed') {
                console.log(`Connection closed with ${id}`);
                updateStatus(`Connection closed with ${id}`);
                isReconnecting = false; // 重置连接状态，允许重连
                // Clean up the peer connection
                if (peerConnectionMap[id] === pc) {
                    delete peerConnectionMap[id];
                }
                if (dataChannelMap[id]) {
                    delete dataChannelMap[id];
                }
                // 连接关闭时立即重连
                if (!reconnectInterval) {
                    startAutoReconnect(ws, id);
                }
            }
        };

        pc.onicecandidate = (e) => {
            if (e.candidate) {
                console.log('Generated ICE candidate:', e.candidate);
                // Send candidate
                sendLocalCandidate(ws, id, e.candidate);
            } else {
                console.log('ICE candidate gathering complete');
            }
        };

        pc.ontrack = (e) => {
            console.log('Received remote track:', e.track.kind, e.track.id, e.track.readyState);

            // ========== 硬件解码优化：检测并配置 ==========
            if (e.track.kind === 'video') {
                // 配置接收端低延迟参数（立即执行）
                if (e.transceiver) {
                    // 立即配置
                    configureLowLatency(e.transceiver);

                    // 延迟再次配置（确保参数已完全初始化）
                    setTimeout(() => {
                        configureLowLatency(e.transceiver);
                    }, 500);

                    // 再次延迟配置（某些浏览器需要多次尝试）
                    setTimeout(() => {
                        configureLowLatency(e.transceiver);
                    }, 2000);
                }

                // 检测视频轨道的编解码器
                if (e.transceiver && e.transceiver.receiver) {
                    try {
                        const receiver = e.transceiver.receiver;
                        const params = receiver.getParameters();
                        if (params && params.codecs) {
                            console.log('Available codecs:', params.codecs.map(c => c.mimeType));

                            // 如果支持硬件解码，优先选择硬件编解码器
                            if (PerformanceOptimizer.preferredCodec) {
                                const preferredCodec = params.codecs.find(c =>
                                    c.mimeType.includes(PerformanceOptimizer.preferredCodec) ||
                                    PerformanceOptimizer.preferredCodec.includes(c.mimeType)
                                );
                                if (preferredCodec) {
                                    console.log('Using preferred codec:', preferredCodec.mimeType);
                                }
                            }
                        }

                        // 尝试直接设置播放延迟（如果 API 支持）
                        if (params) {
                            // 某些浏览器可能支持直接设置
                            if ('minPlayoutDelay' in params || 'playoutDelayHint' in params) {
                                console.log('Attempting to set playout delay directly');
                            }
                        }
                    } catch (err) {
                        console.warn('Failed to inspect codec:', err);
                    }
                }
            }

            // Handle both audio and video tracks from remote peer
            // 检查是否已经设置了媒体源
            if (remoteVideo.srcObject) {
                console.log('Media source already set, adding track:', e.track.kind);

                const stream = remoteVideo.srcObject;

                // 移除同类型的现有轨道（避免重复）
                const existingTracks = stream.getTracks().filter(track => track.kind === e.track.kind);
                existingTracks.forEach(track => {
                    stream.removeTrack(track);
                    track.stop(); // 停止旧的轨道
                });

                // 添加新的轨道
                stream.addTrack(e.track);
                console.log(`Added ${e.track.kind} track to existing stream`);

                // Update track status when tracks are replaced
                setTimeout(updateTrackStatus, 100);
            } else {
                // 创建新的媒体流，包含这个轨道
                console.log(`Creating new stream with ${e.track.kind} track`);
                const stream = new MediaStream([e.track]);
                remoteVideo.srcObject = stream;
                toggleNoSignalOverlay(false); // 当有媒体流时隐藏"无信号"覆盖层

                // Update track status when new stream is created
                setTimeout(updateTrackStatus, 100);
            }

            // 设置轨道属性
            e.track.enabled = true;
            console.log(`${e.track.kind} track enabled:`, e.track.enabled, 'muted:', e.track.muted);

            // 设置视频元素属性
            remoteVideo.muted = false; // 取消视频元素的静音
            remoteVideo.volume = 1.0; // 设置音频音量（如果有音频轨道）

            // ========== 视频轨道特殊优化 ==========
            if (e.track.kind === 'video') {
                // 设置 peer connection 到优化器
                PerformanceOptimizer.setPeerConnection(pc);

                // 应用缓冲区最小化
                PerformanceOptimizer.adaptBufferSize();
            }

            // 添加媒体事件监听
            remoteVideo.onloadedmetadata = () => {
                console.log('Media metadata loaded');
                if (e.track.kind === 'video') {
                    console.log('Video dimensions:', remoteVideo.videoWidth, 'x', remoteVideo.videoHeight);
                }
                updateVideoInfo(remoteVideo.srcObject);
            };

            remoteVideo.onloadeddata = () => {
                console.log('Media data loaded');
                playMedia();
            };

            remoteVideo.onpause = () => {
                console.log('Media paused');
                // 只有在确实没有媒体流时才显示无信号
                if (!remoteVideo.srcObject || remoteVideo.srcObject.getTracks().length === 0) {
                    toggleNoSignalOverlay(true);
                }
            };

            remoteVideo.onended = () => {
                console.log('Media ended');
                toggleNoSignalOverlay(true);
            };

            // 播放媒体
            const playMedia = () => {
                console.log('Attempting to play media...');
                remoteVideo.play().then(() => {
                    console.log('Media playback started successfully');
                    updateStatus(`${e.track.kind} playing`);
                    toggleNoSignalOverlay(false); // 当媒体播放时隐藏"无信号"覆盖层

                    // 再次检查轨道状态
                    setTimeout(() => {
                        const tracks = remoteVideo.srcObject.getTracks();
                        tracks.forEach((track, index) => {
                            console.log(`Track ${index}: kind=${track.kind}, enabled=${track.enabled}, muted=${track.muted}, readyState=${track.readyState}`);
                        });
                    }, 1000);

                }).catch(err => {
                    console.error('Error playing media:', err);
                    updateStatus('Media play failed: ' + err.message);
                    // 如果自动播放失败，显示播放按钮
                    if (err.name === 'NotAllowedError') {
                        showPlayButton();
                    }
                    // 如果自动播放失败，我们可能仍然有媒体流但无法播放
                    if (!remoteVideo.srcObject) {
                        toggleNoSignalOverlay(true);
                    }
                });
            };

            // 立即尝试播放
            setTimeout(playMedia, 100);
        };

        pc.ondatachannel = (e) => {
            const dc = e.channel;
            console.log(`DataChannel from ${id} received with label "${dc.label}"`);
            setupDataChannel(dc, id);

            dc.send(`Hello from ${localId}`);

        };

        peerConnectionMap[id] = pc;
        return pc;
    }

    // Setup a DataChannel
    function setupDataChannel(dc, id) {
        dc.onopen = () => {
            console.log(`DataChannel from ${id} open`);
            updateStatus(`Data channel open with ${id}`);
            stopAutoReconnect(); // 数据通道打开，停止自动重连
        };
        dc.onclose = () => {
            console.log(`DataChannel from ${id} closed`);
            updateStatus(`Data channel closed with ${id}`);
            // 数据通道关闭不立即重连，等待连接状态变化处理
        };
        dc.onerror = (err) => {
            console.error(`DataChannel error with ${id}:`, err);
            updateStatus(`Data channel error with ${id}`);
            // 数据通道错误时可能需要重连
            if (!reconnectInterval) {
                // 延迟检查是否需要重连
                setTimeout(() => {
                    const pc = peerConnectionMap[id];
                    if (!pc || pc.connectionState !== 'connected') {
                        startAutoReconnect(dc._ws, id);
                    }
                }, 2000);
            }
        };
        dc.onmessage = (e) => {
            if (typeof (e.data) != 'string')
                return;
            console.log(`Message from ${id} received: ${e.data}`);
            // Create message element
            const messageElement = document.createElement('div');
            messageElement.textContent = `From ${id}: ${e.data}`;
            messageElement.style.padding = '5px';
            messageElement.style.borderBottom = '1px solid #eee';
            document.body.appendChild(messageElement);
        };

        dataChannelMap[id] = dc;
        return dc;
    }

    // 自动重连功能：快速重连尝试
    function startAutoReconnect(ws, id) {
        // 如果已经在重连，先停止
        stopAutoReconnect();

        updateStatus(`Auto-reconnecting to ${id}...`);
        console.log(`Starting auto-reconnect to ${id}`);

        let reconnectAttempts = 0;
        const maxReconnectAttempts = 120; // 最多重连120次
        let reconnectDelay = 500; // 初始重连延迟500ms（不要太快，避免ICE冲突）
        const maxReconnectDelay = 3000; // 最大重连延迟

        const tryReconnect = () => {
            reconnectAttempts++;

            // 检查是否已超过最大重连次数
            if (reconnectAttempts > maxReconnectAttempts) {
                console.log(`Max reconnection attempts (${maxReconnectAttempts}) reached`);
                stopAutoReconnect();
                updateStatus(`Reconnection failed with ${id}`);
                return;
            }

            // 检查 signaling 连接是否正常
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                console.log('Signaling connection lost, cannot reconnect');
                return;
            }

            // 检查是否已经有成功的连接
            const existingPc = peerConnectionMap[id];
            if (existingPc) {
                const connectionState = existingPc.connectionState;
                const iceState = existingPc.iceConnectionState;

                // 只有真正连接成功了才停止重连
                if (connectionState === 'connected' && (iceState === 'connected' || iceState === 'completed')) {
                    console.log(`Successfully connected to ${id}, stopping reconnection`);
                    stopAutoReconnect();
                    return;
                }

                // 如果正在连接中（checking 或 connecting），不要创建新连接
                if (connectionState === 'connecting' || connectionState === 'new' ||
                    iceState === 'checking' || iceState === 'new') {
                    console.log(`Connection to ${id} in progress (${connectionState}/${iceState}), waiting...`);
                    isReconnecting = true;

                    // 设置超时：如果3秒后还在连接，强制重置
                    setTimeout(() => {
                        if (peerConnectionMap[id] === existingPc) {
                            const currentState = existingPc.connectionState;
                            const currentIceState = existingPc.iceConnectionState;
                            if (currentState === 'connecting' || currentState === 'new' ||
                                currentIceState === 'checking' || currentIceState === 'new') {
                                console.log(`Connection stuck for ${id}, forcing close`);
                                try {
                                    existingPc.close();
                                } catch (e) {
                                    console.warn('Error closing stuck peer connection:', e);
                                }
                                delete peerConnectionMap[id];
                                if (dataChannelMap[id]) {
                                    delete dataChannelMap[id];
                                }
                                isReconnecting = false; // 重置状态，允许重连
                            }
                        }
                    }, 3000);

                    return;
                }

                // 如果是 failed 或 disconnected 状态，清理旧连接
                if (connectionState === 'failed' || connectionState === 'disconnected' ||
                    connectionState === 'closed') {
                    console.log(`Cleaning up old connection in ${connectionState} state`);
                    try {
                        existingPc.close();
                    } catch (e) {
                        console.warn('Error closing old peer connection:', e);
                    }
                    delete peerConnectionMap[id];
                    if (dataChannelMap[id]) {
                        delete dataChannelMap[id];
                    }
                }
            }

            console.log(`Reconnection attempt ${reconnectAttempts}/${maxReconnectAttempts} to ${id} (delay: ${reconnectDelay}ms)`);

            // 标记正在连接
            isReconnecting = true;

            // 创建新的 PeerConnection 并发送 offer
            try {
                offerPeerConnection(ws, id);
            } catch (e) {
                console.error('Error during reconnection:', e);
                isReconnecting = false;
            }

            // 调整重连延迟（线性增加，避免指数退避太快）
            if (reconnectDelay < maxReconnectDelay) {
                reconnectDelay = Math.min(maxReconnectDelay, reconnectDelay + 100);
            }
        };

        // 立即尝试第一次连接
        tryReconnect();

        // 设置定时器继续重连
        reconnectInterval = setInterval(tryReconnect, reconnectDelay);
    }

    function stopAutoReconnect() {
        if (reconnectInterval) {
            clearInterval(reconnectInterval);
            reconnectInterval = null;
            console.log('Video auto-reconnect stopped');
        }
        // 重置连接状态
        isReconnecting = false;
    }

    // WebSocket 自动重连功能
    function startWsReconnect(url) {
        // 如果已经在重连，先停止
        stopWsReconnect();

        updateStatus('Reconnecting to signaling server...');
        console.log('Starting WebSocket reconnection to:', url);

        wsReconnectInterval = setInterval(() => {
            console.log('Attempting to reconnect to signaling server...');
            openSignaling(url)
                .then(async (ws) => {
                    console.log('Signaling server reconnected');
                    stopWsReconnect();
                    updateStatus('Signaling connected');
                    offerId.disabled = false;
                    offerBtn.disabled = false;
                    offerBtn.onclick = () => offerPeerConnection(ws, offerId.value);
                    // 如果有远程 ID，自动尝试连接
                    if (offerId.value) {
                        setTimeout(() => {
                            offerPeerConnection(ws, offerId.value);
                        }, 1000);
                    }
                })
                .catch((err) => {
                    console.error('Signaling reconnection failed:', err.message);
                });
        }, 3000); // 每 3 秒重试一次
    }

    function stopWsReconnect() {
        if (wsReconnectInterval) {
            clearInterval(wsReconnectInterval);
            wsReconnectInterval = null;
            console.log('WebSocket auto-reconnect stopped');
        }
    }

    function sendLocalDescription(ws, id, pc, type) {
        const options = type === 'offer' ? {
            offerToReceiveAudio: true,
            offerToReceiveVideo: true
        } : {};

        (type == 'offer' ? pc.createOffer(options) : pc.createAnswer())
        .then((desc) => {
                // console.log(`Created ${type}:`, desc.sdp);

                // ========== SDP 优化：低延迟配置 ==========
                // 修改 SDP 以优化延迟
                if (desc.sdp) {
                    let sdp = desc.sdp;

                    // ========== 关键优化：降低抖动缓冲区延迟 ==========
                    // 添加 Google 特定的播放延迟控制（Chrome/Edge 支持）
                    // 目标：将 jitter buffer 延迟从 226ms 降低到 30-50ms

                    // 查找视频媒体行（m=video）
                    const videoMediaMatch = sdp.match(/m=video\s+\d+\s+[^\r\n]+/);
                    if (videoMediaMatch) {
                        const videoMediaLine = videoMediaMatch[0];
                        const videoMediaIndex = sdp.indexOf(videoMediaLine);

                        // 在视频媒体行后查找第一个属性行
                        let insertPosition = sdp.indexOf('\r\n', videoMediaIndex) + 2;

                        // 检查是否已存在播放延迟设置
                        if (!sdp.includes('x-google-min-playout-delay-ms') &&
                            !sdp.includes('x-google-max-playout-delay-ms')) {

                            // 插入最小播放延迟（30ms）
                            const minDelayLine = 'a=x-google-min-playout-delay-ms:20\r\n';
                            // 插入最大播放延迟（100ms）
                            const maxDelayLine = 'a=x-google-max-playout-delay-ms:50\r\n';

                            // 在视频媒体行后插入延迟设置
                            sdp = sdp.slice(0, insertPosition) +
                                minDelayLine +
                                maxDelayLine +
                                sdp.slice(insertPosition);

                            console.log('Added jitter buffer delay control to SDP (20-50ms)');
                        }
                    }

                    // 优先使用硬件编解码器
                    if (PerformanceOptimizer.preferredCodec) {
                        // 重新排序编解码器，将首选编解码器放在前面
                        const codecPattern = PerformanceOptimizer.preferredCodec.includes('h264') ?
                            /(a=rtpmap:\d+ (h264|H264)[^\r\n]*)/g :
                            /(a=rtpmap:\d+ (vp8|VP8)[^\r\n]*)/g;

                        // 这里可以添加 SDP 修改逻辑（如果需要）
                        console.log('Applying preferred codec optimization');
                    }

                    // 添加低延迟标志（某些浏览器支持）
                    // 对于视频会议场景，可以添加 conference 标志
                    if (type === 'offer' && !sdp.includes('x-google-flag:conference')) {
                        // 在视频媒体描述中添加低延迟标志
                        sdp = sdp.replace(/(m=video[^\r\n]+\r\n)/,
                            '$1a=x-google-flag:conference\r\n');
                    }

                    desc.sdp = sdp;
                }

                return pc.setLocalDescription(desc);
            })
            .then(() => {
                const {
                    sdp,
                    type
                } = pc.localDescription;
                console.log(`Sending ${type} to ${id}`);
                ws.send(JSON.stringify({
                    id,
                    type,
                    description: sdp,
                }));
            })
            .catch(err => {
                console.error(`Error creating ${type}:`, err);
                updateStatus(`Error creating ${type}: ${err.message}`);
            });
    }

    function sendLocalCandidate(ws, id, cand) {
        const {
            candidate,
            sdpMid,
            sdpMLineIndex
        } = cand;
        console.log('Sending ICE candidate to', id);
        ws.send(JSON.stringify({
            id,
            type: 'candidate',
            candidate,
            mid: sdpMid,
            sdpMLineIndex: sdpMLineIndex
        }));
    }

    function updateVideoInfo(stream) {
        const videoInfo = document.getElementById('videoInfo');
        if (!videoInfo) return;

        const tracks = stream.getTracks();
        const videoTracks = stream.getVideoTracks();
        const audioTracks = stream.getAudioTracks();

        let info = `
        <p><strong>Total Tracks:</strong> ${tracks.length}</p>
        <p><strong>Video Tracks:</strong> ${videoTracks.length}</p>
        <p><strong>Audio Tracks:</strong> ${audioTracks.length}</p>
        <p><strong>Media Ready State:</strong> ${remoteVideo.readyState}</p>
        <p><strong>Video Dimensions:</strong> ${remoteVideo.videoWidth} x ${remoteVideo.videoHeight}</p>
        <p><strong>Media Muted:</strong> ${remoteVideo.muted}</p>
        <p><strong>Volume:</strong> ${remoteVideo.volume}</p>
        <p><strong>Current Time:</strong> ${remoteVideo.currentTime}</p>
        <p><strong>Network State:</strong> ${remoteVideo.networkState}</p>
    `;

        videoTracks.forEach((track, index) => {
            info += `
            <p><strong>Video Track ${index}:</strong> 
                id=${track.id}, 
                enabled=${track.enabled}, 
                readyState=${track.readyState},
                muted=${track.muted}
            </p>
        `;
        });

        audioTracks.forEach((track, index) => {
            info += `
            <p><strong>Audio Track ${index}:</strong> 
                id=${track.id}, 
                enabled=${track.enabled}, 
                readyState=${track.readyState},
                muted=${track.muted}
            </p>
        `;
        });

        videoInfo.innerHTML = info;
    }


    // // 定期更新视频信息
    // setInterval(() => {
    //     if (remoteVideo.srcObject) {
    //         updateVideoInfo(remoteVideo.srcObject);

    //         // ========== 性能监控输出 ==========
    //         const metrics = PerformanceOptimizer.getMetrics();
    //         if (metrics.totalLatency > 0) {
    //             console.log('Performance Metrics:', {
    //                 decodeLatency: metrics.decodeLatency.toFixed(2) + 'ms',
    //                 bufferDelay: metrics.bufferDelay.toFixed(2) + 'ms',
    //                 renderLatency: metrics.renderLatency.toFixed(2) + 'ms',
    //                 totalLatency: metrics.totalLatency.toFixed(2) + 'ms',
    //             });
    //         }
    //     }
    // }, 2000);

    // ========== 自适应优化：根据性能调整 ==========
    let adaptiveOptimizationInterval = null;

    function startAdaptiveOptimization() {
        // 如果已经在运行，先停止
        if (adaptiveOptimizationInterval) {
            clearInterval(adaptiveOptimizationInterval);
        }

        adaptiveOptimizationInterval = setInterval(async () => {
            if (!remoteVideo || !remoteVideo.srcObject) return;

            // 获取所有活跃的 peer connections
            const activeConnections = Object.values(peerConnectionMap).filter(
                pc => pc && pc.connectionState === 'connected'
            );

            if (activeConnections.length === 0) return;

            // 使用第一个活跃连接
            const pc = activeConnections[0];

            // 设置 peer connection 到优化器
            PerformanceOptimizer.setPeerConnection(pc);

            // 从 WebRTC stats 更新性能指标
            const metrics = await PerformanceOptimizer.getMetrics(true);

            // ========== 自适应优化策略 ==========

            // 策略 1: 高延迟处理（> 150ms）
            if (metrics.totalPlaybackDelay > 150) {
                console.warn('⚠️ 高延迟检测:', {
                    '总延迟': metrics.totalPlaybackDelay.toFixed(2) + 'ms',
                    '抖动缓冲': metrics.avgJitterBufferDelay.toFixed(2) + 'ms',
                    '视频缓冲': metrics.videoBufferDelay.toFixed(2) + 'ms',
                    '解码延迟': metrics.avgDecodeTime.toFixed(2) + 'ms'
                });

                // 如果抖动缓冲区延迟过高，增加缓冲区以抗抖动
                if (metrics.avgJitterBufferDelay > 50) {
                    PerformanceOptimizer.adaptBufferSize(100, metrics.packetLossRate);
                    console.log('📈 应用抗抖动模式：增加缓冲区');
                }

                // 如果视频元素缓冲过大，尝试减小
                if (metrics.videoBufferDelay > 200) {
                    console.log('📉 视频元素缓冲过大，TODO：尝试优化');
                    // 可以尝试调整播放速率或跳过缓冲
                }
            }
            // 策略 2: 低延迟优化（< 80ms）- 可以更激进
            else if (metrics.totalPlaybackDelay < 80) {
                // 如果延迟很低且网络稳定，可以进一步优化
                if (metrics.packetLossRate < 0.01 && metrics.avgJitterBufferDelay < 30) {
                    PerformanceOptimizer.adaptBufferSize(30, 0);
                    // console.log('✅ 低延迟模式：进一步优化缓冲区');
                }
            }

            // 策略 3: 丢帧处理
            if (metrics.framesDropped > 0) {
                const dropRate = metrics.framesDropped / (metrics.framesDecoded + metrics.framesDropped);
                if (dropRate > 0.05) { // 丢帧率 > 5%
                    console.warn('⚠️ 高丢帧率:', (dropRate * 100).toFixed(2) + '%');
                    // 增加缓冲区以减少丢帧
                    PerformanceOptimizer.adaptBufferSize(150, metrics.packetLossRate);
                }
            }

            // 策略 4: 帧率下降处理
            if (metrics.framesPerSecond > 0 && metrics.framesPerSecond < 25) {
                console.warn('⚠️ 帧率下降:', metrics.framesPerSecond.toFixed(2) + 'fps');
                // 可以尝试降低分辨率或调整编码参数
            }

            // 策略 5: 丢包处理
            if (metrics.packetLossRate > 0.02) { // 丢包率 > 2%
                console.warn('⚠️ 高丢包率:', (metrics.packetLossRate * 100).toFixed(2) + '%');
                // 增加缓冲区以应对网络抖动
                PerformanceOptimizer.adaptBufferSize(200, metrics.packetLossRate);
            }

        }, 5000); // 每 5 秒检查一次
    }

    // 启动自适应优化
    startAdaptiveOptimization();

    // 页面卸载时停止自动重连
    window.addEventListener('beforeunload', () => {
        stopAutoReconnect();
        stopWsReconnect(); // 页面卸载时停止 WebSocket 重连
    });

    // Helper function to generate a random ID
    function randomId(length) {
        const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
        return [...Array(length)].map(pickRandom).join('');
    }
});