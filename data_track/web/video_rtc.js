window.addEventListener('load', () => {

    const localId = randomId(4);
    const url = `ws://fy403.cn:8000/${localId}`;
    const config = {
        iceServers: [{
                "urls": ["stun:stun.cloudflare.com:3478",
                    // "stun:stun.cloudflare.com:53"
                ]
            },
            {
                "urls": [
                    "turn:turn.cloudflare.com:3478?transport=udp",
                    // "turn:turn.cloudflare.com:3478?transport=tcp",
                    // "turns:turn.cloudflare.com:5349?transport=tcp",
                    // "turn:turn.cloudflare.com:53?transport=udp",
                    // "turn:turn.cloudflare.com:80?transport=tcp",
                    // "turns:turn.cloudflare.com:443?transport=tcp"
                ],
                "username": "g0xxxxxxxxxxx",
                "credential": "95yyyyyyyyy"
            },
        ],
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

    // Initialize with "No Signal" overlay visible
    toggleNoSignalOverlay(true);

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
                console.log(`Created ${type}:`, desc.sdp);
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

    // 定期更新视频信息
    setInterval(() => {
        if (remoteVideo.srcObject) {
            updateVideoInfo(remoteVideo.srcObject);
        }
    }, 2000);

    // Helper function to generate a random ID
    function randomId(length) {
        const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
        return [...Array(length)].map(pickRandom).join('');
    }
});