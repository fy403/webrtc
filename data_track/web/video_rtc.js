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

    const offerId = document.getElementById('offerId');
    const offerBtn = document.getElementById('offerBtn');
    const _localId = document.getElementById('localId');
    const remoteVideo = document.getElementById('remoteVideo');
    const statusDiv = document.getElementById('status');
    _localId.textContent = localId;

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
                video: false,
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
            if (offerId.value) {
                offerBtn.click();
            }
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
            ws.onopen = () => resolve(ws);
            ws.onerror = () => reject(new Error('WebSocket error'));
            ws.onclose = () => {
                console.error('WebSocket disconnected');
                updateStatus('Signaling disconnected');
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
                        pc.setRemoteDescription(new RTCSessionDescription({
                            sdp: message.description,
                            type: message.type,
                        })).then(() => {
                            console.log(`Set remote ${type} successfully`);
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
                        pc.addIceCandidate(new RTCIceCandidate({
                            candidate: message.candidate,
                            sdpMid: message.mid,
                            sdpMLineIndex: message.sdpMLineIndex
                        })).then(() => {
                            console.log('Added ICE candidate successfully');
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
        setupDataChannel(dc, id);

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
                updateStatus(`Connected to ${id}`);
            } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
                updateStatus(`Connection failed with ${id}`);
                // Show "No Signal" overlay when connection fails
                toggleNoSignalOverlay(true);
            }
        };

        pc.onconnectionstatechange = () => {
            console.log(`Connection state: ${pc.connectionState}`);
            updateStatus(`Connection: ${pc.connectionState}`);

            if (pc.connectionState === 'connected') {
                updateStatus(`Connected to ${id}`);
            } else if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected' || pc.connectionState === 'closed') {
                updateStatus(`Connection failed with ${id}`);
                // Show "No Signal" overlay when connection fails
                toggleNoSignalOverlay(true);
                // Clean up the peer connection
                if (peerConnectionMap[id]) {
                    delete peerConnectionMap[id];
                }
                if (dataChannelMap[id]) {
                    delete dataChannelMap[id];
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
            } else {
                // 创建新的媒体流，包含这个轨道
                console.log(`Creating new stream with ${e.track.kind} track`);
                const stream = new MediaStream([e.track]);
                remoteVideo.srcObject = stream;
                toggleNoSignalOverlay(false); // 当有媒体流时隐藏"无信号"覆盖层
            }

            // 设置轨道属性
            e.track.enabled = true;
            console.log(`${e.track.kind} track enabled:`, e.track.enabled, 'muted:', e.track.muted);

            // 设置视频元素属性
            remoteVideo.muted = false; // 取消视频元素的静音
            remoteVideo.volume = 1.0; // 设置音频音量（如果有音频轨道）

            // ========== 视频轨道特殊优化 ==========
            if (e.track.kind === 'video') {
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
        };
        dc.onclose = () => {
            console.log(`DataChannel from ${id} closed`);
            updateStatus(`Data channel closed with ${id}`);
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
    setInterval(() => {
        if (!remoteVideo || !remoteVideo.srcObject) return;
        
        const metrics = PerformanceOptimizer.getMetrics();
        
        // 如果总延迟过高，采取应急措施
        if (metrics.totalLatency > 150) {
            console.warn('High latency detected, applying emergency optimizations');
            
            // 可以在这里添加应急优化措施
            // 例如：降低分辨率、增加缓冲区等
            PerformanceOptimizer.adaptBufferSize(100, 0.05); // 增加缓冲区以抗抖动
        } 
        // 如果延迟很低，可以尝试更激进的优化
        else if (metrics.totalLatency < 80) {
            // 可以在这里添加更多优化措施
        }
    }, 5000);

    // Helper function to generate a random ID
    function randomId(length) {
        const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
        return [...Array(length)].map(pickRandom).join('');
    }
});