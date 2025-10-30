window.addEventListener('load', () => {

    const localId = randomId(4);
    const url = `ws://fy403.cn:8000/${localId}`;
    const config = {
        iceServers: [{
            "urls": [
                "stun:stun.l.google.com:19302",
                "stun:stun.cloudflare.com:3478",
                // "stun:stun.cloudflare.com:53"
            ]
        },
            {
                "urls": [
                    // "turn:turn.cloudflare.com:3478?transport=udp",
                    // "turn:turn.cloudflare.com:3478?transport=tcp",
                    // "turns:turn.cloudflare.com:5349?transport=tcp",
                    "turn:turn.cloudflare.com:53?transport=udp",
                    // "turn:turn.cloudflare.com:80?transport=tcp",
                    // "turns:turn.cloudflare.com:443?transport=tcp"
                ],
                "username": "g01ca9a98e4b987d196492b8da9873bde96eb447d5587a708916ffad6daa4ac5",
                "credential": "ad5db8205573fa6f8b2a93c1c04fecceab6dc8b3b4ec9bb5db5c386305e49610"
            },
        ],
    };

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

    console.log('Connecting to signaling...');
    openSignaling(url)
        .then((ws) => {
            console.log('WebSocket connected, signaling ready');
            updateStatus('Signaling connected');
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

            // 处理音频和视频轨道
            if (e.track.kind === 'audio') {
                console.log('Processing audio track');
                // 音频轨道处理
                handleAudioTrack(e.track);
            } else if (e.track.kind === 'video') {
                console.log('Processing video track');
                // 视频轨道处理
                handleVideoTrack(e.track);
            }
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

    // 处理音频轨道
    function handleAudioTrack(track) {
        const audioElement = document.getElementById('remoteAudio');

        // 为音频元素创建独立的媒体流
        let stream = audioElement.srcObject;
        if (!stream) {
            stream = new MediaStream();
            audioElement.srcObject = stream;
        } else {
            // 清除现有的音频轨道
            const audioTracks = stream.getAudioTracks();
            audioTracks.forEach(t => stream.removeTrack(t));
        }

        // 添加新的音频轨道到音频流中
        stream.addTrack(track);
        console.log('Added audio track to audio stream');

        // 更新音频信息显示
        updateAudioInfo(stream);

        // 尝试播放音频
        const playPromise = audioElement.play();
        if (playPromise !== undefined) {
            playPromise
                .then(() => {
                    console.log('Audio playback started successfully');
                    updateStatus('Audio playing');
                })
                .catch(err => {
                    console.error('Error playing audio:', err);
                    updateStatus('Audio play failed: ' + err.message);
                    // 显示用户需要交互的提示
                    if (err.name === 'NotAllowedError') {
                        showAudioPermissionPrompt(audioElement);
                    }
                });
        }
    }

    // 显示音频权限提示
    function showAudioPermissionPrompt(audioElement) {
        // 查找或创建提示元素
        let prompt = document.getElementById('audio-permission-prompt');
        if (!prompt) {
            prompt = document.createElement('div');
            prompt.id = 'audio-permission-prompt';
            prompt.innerHTML = `
                <div style="position: fixed; bottom: 20px; right: 20px; background: #333; color: white; padding: 15px; border-radius: 5px; z-index: 1000;">
                    <p>音频需要用户交互才能播放</p>
                    <button id="play-audio-btn" style="background: #4CAF50; border: none; color: white; padding: 10px 15px; cursor: pointer; border-radius: 3px;">点击播放音频</button>
                </div>
            `;
            document.body.appendChild(prompt);

            // 添加按钮事件监听器
            document.getElementById('play-audio-btn').addEventListener('click', () => {
                const playPromise = audioElement.play();
                if (playPromise !== undefined) {
                    playPromise
                        .then(() => {
                            console.log('Audio playback started successfully after user interaction');
                            // 移除提示
                            if (prompt.parentNode) {
                                prompt.parentNode.removeChild(prompt);
                            }
                        })
                        .catch(err => {
                            console.error('Error playing audio after user interaction:', err);
                        });
                }
            });
        }
    }

    // 处理视频轨道
    function handleVideoTrack(track) {
        // 检查是否已经设置了视频源
        if (remoteVideo.srcObject) {
            console.log('Video source already set, replacing track');

            // 获取视频元素的流并移除现有的视频轨道
            const stream = remoteVideo.srcObject;
            const videoTracks = stream.getVideoTracks();

            // 移除所有现有的视频轨道
            videoTracks.forEach(track => {
                stream.removeTrack(track);
                track.stop(); // 停止旧的轨道
            });

            // 添加新的视频轨道
            stream.addTrack(track);
            console.log('Replaced video track');
        } else {
            // 创建新的媒体流，仅包含这个视频轨道
            console.log('Creating new stream with video track');
            const stream = new MediaStream([track]);
            remoteVideo.srcObject = stream;
            toggleNoSignalOverlay(false); // Hide "No Signal" overlay when video stream is set
        }

        // 强制启用视频轨道
        track.enabled = true;
        console.log('Video track enabled:', track.enabled, 'muted:', track.muted);

        // 设置视频属性
        remoteVideo.muted = false; // 取消视频元素的静音

        // 添加视频事件监听
        remoteVideo.onloadedmetadata = () => {
            console.log('Video metadata loaded');
            console.log('Video dimensions:', remoteVideo.videoWidth, 'x', remoteVideo.videoHeight);
            updateVideoInfo(remoteVideo.srcObject);
        };

        remoteVideo.onloadeddata = () => {
            console.log('Video data loaded');
            playVideo();
        };

        remoteVideo.onpause = () => {
            console.log('Video paused');
            // Only show no signal if there's actually no stream
            if (!remoteVideo.srcObject || remoteVideo.srcObject.getVideoTracks().length === 0) {
                toggleNoSignalOverlay(true);
            }
        };

        remoteVideo.onended = () => {
            console.log('Video ended');
            toggleNoSignalOverlay(true);
        };

        // 播放视频
        const playVideo = () => {
            console.log('Attempting to play video...');
            remoteVideo.play().then(() => {
                console.log('Video playback started successfully');
                updateStatus('Video playing');
                toggleNoSignalOverlay(false); // Hide "No Signal" overlay when video plays

                // 再次检查轨道状态
                setTimeout(() => {
                    if (remoteVideo.srcObject) {
                        const tracks = remoteVideo.srcObject.getTracks();
                        tracks.forEach((track, index) => {
                            console.log(`Video Track ${index}: kind=${track.kind}, enabled=${track.enabled}, muted=${track.muted}`);
                        });
                    }
                }, 1000);

            }).catch(err => {
                console.error('Error playing video:', err);
                updateStatus('Video play failed: ' + err.message);
                // If autoplay failed, we might still have a stream but can't play it
                if (!remoteVideo.srcObject) {
                    toggleNoSignalOverlay(true);
                }
            });
        };

        // 立即尝试播放
        setTimeout(playVideo, 100);
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
        <p><strong>Video Ready State:</strong> ${remoteVideo.readyState}</p>
        <p><strong>Video Dimensions:</strong> ${remoteVideo.videoWidth} x ${remoteVideo.videoHeight}</p>
        <p><strong>Video Muted:</strong> ${remoteVideo.muted}</p>
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

        videoInfo.innerHTML = info;
    }

    function updateAudioInfo(stream) {
        const audioInfo = document.getElementById('audioInfo');
        if (!audioInfo) return;

        const audioTracks = stream.getAudioTracks();

        let info = `<h4>Audio Track Info:</h4>`;

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

        audioInfo.innerHTML = info;
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