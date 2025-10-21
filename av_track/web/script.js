/*
 * libdatachannel example web client
 * Copyright (C) 2020 Lara Mackey
 * Copyright (C) 2020 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

window.addEventListener('load', () => {

    const config = {
        iceServers: [{
            urls: 'stun:stun1.l.google.com:19302',
        }],
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
        })
        .catch((err) => {
            console.error(err);
            updateStatus('Signaling connection failed: ' + err.message);
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

        // 在 ontrack 事件中添加更多日志
        pc.ontrack = (e) => {
            console.log('Received remote track:', e.track.kind, e.track.id);

            // 检查是否已经设置了视频源
            if (remoteVideo.srcObject) {
                console.log('Video source already set, skipping...');
                return;
            }

            if (e.streams && e.streams.length > 0) {
                const stream = e.streams[0];
                console.log('Setting remote video source');

                remoteVideo.srcObject = stream;

                // 添加播放事件监听
                const playVideo = () => {
                    remoteVideo.play().then(() => {
                        console.log('Video playback started successfully');
                    }).catch(err => {
                        console.error('Error playing video:', err);
                        // 如果自动播放失败，等待用户交互
                        if (err.name === 'NotAllowedError') {
                            console.log('Waiting for user interaction to play video');
                            // 可以添加一个播放按钮让用户手动触发
                        }
                    });
                };

                // 如果视频已经可以播放，立即尝试播放
                if (remoteVideo.readyState >= 2) { // HAVE_CURRENT_DATA
                    playVideo();
                } else {
                    remoteVideo.onloadeddata = playVideo;
                }

                // 添加其他事件监听用于调试
                remoteVideo.oncanplay = () => console.log('Video can play');
                remoteVideo.oncanplaythrough = () => console.log('Video can play through');
                remoteVideo.onerror = (e) => console.error('Video error:', e);
            }
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

    // Helper function to generate a random ID
    function randomId(length) {
        const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
        return [...Array(length)].map(pickRandom).join('');
    }

});