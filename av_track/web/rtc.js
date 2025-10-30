window.addEventListener('load', () => {

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
                "username": "g035d939b93f4d9303ff74e5c5135deb891345ee621b1ac4cde334f062450e4a",
                "credential": "95575f4a4dc4f54f465372dc2b44999e7a61013545fc5a8d1930bc20a981c70e"
            },
        ],
    };

    const localId = randomId(4);

    const url = `ws://fy403.cn:8000/${localId}`;

    const peerConnectionMap = {};
    const dataChannelMap = {};

    const offerId = document.getElementById('offerId');
    const offerBtn = document.getElementById('offerBtn');
    const sendMsg = document.getElementById('sendMsg');
    const sendBtn = document.getElementById('sendBtn');
    const _localId = document.getElementById('localId');
    const remoteVideo = document.getElementById('remoteVideo');
    const statusDiv = document.getElementById('status');
    _localId.textContent = localId;

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
            }
        };

        pc.onicegatheringstatechange = () => {
            console.log(`ICE Gathering state: ${pc.iceGatheringState}`);
        };

        pc.onsignalingstatechange = () => {
            console.log(`Signaling state: ${pc.signalingState}`);
        };

        pc.onconnectionstatechange = () => {
            console.log(`Connection state: ${pc.connectionState}`);
            updateStatus(`Connection: ${pc.connectionState}`);
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

            // 如果是音频轨道，直接忽略（因为我们只关心视频）
            if (e.track.kind === 'audio') {
                console.log('Ignoring audio track');
                return;
            }

            // 检查是否已经设置了视频源
            if (remoteVideo.srcObject) {
                console.log('Video source already set, replacing track');

                // 移除现有的视频轨道，只保留音频（如果有的话）
                const stream = remoteVideo.srcObject;
                const videoTracks = stream.getVideoTracks();

                // 移除所有现有的视频轨道
                videoTracks.forEach(track => {
                    stream.removeTrack(track);
                    track.stop(); // 停止旧的轨道
                });

                // 添加新的视频轨道
                stream.addTrack(e.track);
                console.log('Replaced video track');
            } else {
                // 创建新的媒体流，只包含这个视频轨道
                console.log('Creating new stream with video track');
                const stream = new MediaStream([e.track]);
                remoteVideo.srcObject = stream;
            }

            // 强制取消视频静音
            const videoTracks = remoteVideo.srcObject.getVideoTracks();
            videoTracks.forEach(track => {
                track.enabled = true;
                console.log('Video track enabled:', track.enabled, 'muted:', track.muted);
            });

            // 设置视频属性
            remoteVideo.muted = false; // 取消视频元素的静音
            remoteVideo.volume = 0; // 音量设为0，因为我们没有音频

            // 播放视频
            const playVideo = () => {
                console.log('Attempting to play video...');
                remoteVideo.play().then(() => {
                    console.log('Video playback started successfully');
                    updateStatus('Video playing');

                    // 再次检查轨道状态
                    setTimeout(() => {
                        const tracks = remoteVideo.srcObject.getTracks();
                        tracks.forEach((track, index) => {
                            console.log(`Track ${index}: kind=${track.kind}, enabled=${track.enabled}, muted=${track.muted}`);
                        });
                    }, 1000);

                }).catch(err => {
                    console.error('Error playing video:', err);
                    updateStatus('Video play failed: ' + err.message);
                });
            };

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

            // 立即尝试播放
            setTimeout(playVideo, 100);
        };

        pc.ondatachannel = (e) => {
            const dc = e.channel;
            console.log(`DataChannel from ${id} received with label "${dc.label}"`);
            setupDataChannel(dc, id);

            dc.send(`Hello from ${localId}`);

            sendMsg.disabled = false;
            sendBtn.disabled = false;
            sendBtn.onclick = () => {
                if (dc.readyState === 'open') {
                    dc.send(sendMsg.value);
                    sendMsg.value = '';
                }
            };
        };

        peerConnectionMap[id] = pc;
        return pc;
    }

    // Setup a DataChannel
    function setupDataChannel(dc, id) {
        dc.onopen = () => {
            console.log(`DataChannel from ${id} open`);
            updateStatus(`Data channel open with ${id}`);

            sendMsg.disabled = false;
            sendBtn.disabled = false;
            sendBtn.onclick = () => {
                if (dc.readyState === 'open') {
                    dc.send(sendMsg.value);
                    sendMsg.value = '';
                }
            };
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