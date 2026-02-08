window.addEventListener('load', () => {
    // Only keep status frame id from legacy protocol; control uses SBUS.
    const MSG_SYSTEM_STATUS = 0x20;

    // 从配置管理器加载数据配置
    const dataConfig = ConfigManager.getDataConfig();

    const dataLocalId = dataRandomId(5);
    const dataUrl = `${dataConfig.signalingUrl}/${dataLocalId}`;

    const rtcConfig = {
        iceServers: dataConfig.iceServers || [{
            urls: ['stun:stun.l.google.com:19302']
        },
            {
                urls: ['turn:tx.fy403.cn:3478?transport=udp'],
                username: 'fy403',
                credential: 'qwertyuiop'
            },
        ],
    };


    const dataPeerConnectionMap = {};
    const dataDataChannelMap = {};
    let dataCurrentDataChannel = null;
    let dataSignalingWs = null;
    let dataReconnectInterval = null; // PeerConnection 自动重连定时器
    let dataWsReconnectInterval = null; // WebSocket 重连定时器

    const dataOfferId = document.getElementById('dataOfferId');
    const dataOfferBtn = document.getElementById('dataOfferBtn');
    const dataLocalIdElement = document.getElementById('dataLocalId');
    const dataStatusDiv = document.getElementById('dataStatus');
    dataLocalIdElement.textContent = dataLocalId;

    // 从配置加载远程ID
    if (dataOfferId && dataConfig.remoteId) {
        dataOfferId.value = dataConfig.remoteId;
    }

    // UI state
    const dataState = {W: false, A: false, S: false, D: false};
    const dataThrottlePresets = {
        Digit1: {limit: 0.25, label: '25%'},
        Digit2: {limit: 0.5, label: '50%'},
        Digit3: {limit: 0.75, label: '75%'},
        Digit4: {limit: 1.0, label: '100%'},
        Numpad1: {limit: 0.25, label: '25%'},
        Numpad2: {limit: 0.5, label: '50%'},
        Numpad3: {limit: 0.75, label: '75%'},
        Numpad4: {limit: 1.0, label: '100%'},
    };
    let dataThrottleLimit = 1.0; // 默认不限速

    // DOM references
    const dataElements = {
        reconnectBtn: document.getElementById('reconnectBtn'),
        stopAllBtn: document.getElementById('stopAllBtn'),
        emgBtn: document.getElementById('emgBtn'),
        throttleBtn: document.getElementById('throttleBtn'),
        keyW: document.getElementById('keyW'),
        reconnectBtnMobile: document.getElementById('reconnectBtnMobile'),
        stopAllBtnMobile: document.getElementById('stopAllBtnMobile'),
        emgBtnMobile: document.getElementById('emgBtnMobile'),
        throttleBtnMobile: document.getElementById('throttleBtnMobile'),
        keyA: document.getElementById('keyA'),
        keyS: document.getElementById('keyS'),
        keyD: document.getElementById('keyD'),
        joystickContainer: document.getElementById('virtualJoystickContainer'),
        joystickBase: document.getElementById('joystickBase'),
        joystickHandle: document.getElementById('joystickHandle'),
        // Dual joystick elements (for mobile)
        leftJoystickBase: document.getElementById('leftJoystickBase'),
        leftJoystickHandle: document.getElementById('leftJoystickHandle'),
        rightJoystickBase: document.getElementById('rightJoystickBase'),
        rightJoystickHandle: document.getElementById('rightJoystickHandle'),
        connStatus: document.getElementById('connStatus'),
        signalStrength: document.getElementById('signalStrength'),
        signalIndicator: document.getElementById('signalIndicator'),
        signalQuality: document.getElementById('signalQuality'),
        serviceStatus: document.getElementById('serviceStatus'),
        simStatus: document.getElementById('simStatus'),
        rxSpeed: document.getElementById('rxSpeed'),
        txSpeed: document.getElementById('txSpeed'),
        cpuUsage: document.getElementById('cpuUsage'),
        lastUpdate: document.getElementById('lastUpdate'),
        speedValue: document.getElementById('speedValue'),
        throttleLimitIndicator: document.getElementById('throttleLimitIndicator'),
        // Controller status elements
        keyboardStatus: document.getElementById('keyboardStatus'),
        xboxStatus: document.getElementById('xboxStatus'),
        gyroStatus: document.getElementById('gyroStatus'),
    };

    // System status snapshot
    const dataSystemStatus = {
        rxSpeed: 0,
        txSpeed: 0,
        cpuUsage: 0,
        ttyService: false,
        rtspService: false,
        signalStrength: -1,
        sim_status: 'UNKNOWN',
        lastUpdate: null,
    };

    let uiSpeed = 0;
    let lastSentState = { forward: 0, turn: 0 };
    let heartbeatInterval = null;

    // SBUS control pipeline
    const controllerManager = new ControllerManager((state) => {
        lastSentState = { forward: state.forward || 0, turn: state.turn || 0 };
        dataSendSbus(lastSentState.forward, lastSentState.turn);
    });

    function dataApplyThrottleLimit(forward) {
        const safeForward = forward || 0;
        const capped = Math.min(Math.max(safeForward, -dataThrottleLimit), dataThrottleLimit);
        return capped;
    }

    function dataShowThrottleLimitMessage(limit) {
        const percent = Math.round(limit * 100);
        const text = `油门最大比例切换到 ${percent}%`;
        if (dataStatusDiv) dataStatusDiv.textContent = 'Status: ' + text;
        if (dataElements.throttleLimitIndicator) dataElements.throttleLimitIndicator.textContent = `${percent}%`;
        console.log(text);
    }

    function dataSetThrottleLimit(limit, notify = true) {
        if (limit === dataThrottleLimit) return;
        dataThrottleLimit = limit;
        if (notify) dataShowThrottleLimitMessage(limit);
        // 立即按照新的限幅重新发送一次
        dataSendSbus(lastSentState.forward, lastSentState.turn);
    }

    function dataSendSbus(forward, turn) {
        if (!dataCurrentDataChannel || dataCurrentDataChannel.readyState !== 'open') return;
        const limitedForward = dataApplyThrottleLimit(forward);
        uiSpeed = Math.round(Math.abs(limitedForward) * 100);
        dataUpdateSystemStatusDisplay();
        try {
            // 使用新协议 RCProtocol v2，直接传输-1.0~1.0浮点数
            const frame = RCProtocol.encode({ch1: limitedForward, ch2: turn || 0,
                ch3: 0, ch4: 0, ch5: 0, ch6: 0, ch7: 0, ch8: 0,
                ch9: 0, ch10: 0, ch11: 0, ch12: 0, ch13: 0,
                ch14: 0, ch15: 0, ch16: 0});
            dataCurrentDataChannel.send(frame);
            // 更新通道显示
            dataUpdateChannelDisplay();
        } catch (e) {
            console.error('Failed to send RC frame', e);
        }
    }

    // 启动心跳机制：定期发送当前状态，防止丢包导致失控
    // 心跳间隔100ms，小于C++端watchdog超时时间（300ms），确保即使丢包也能及时恢复
    function startHeartbeat() {
        if (heartbeatInterval) clearInterval(heartbeatInterval);
        heartbeatInterval = setInterval(() => {
            if (dataCurrentDataChannel && dataCurrentDataChannel.readyState === 'open') {
                // 定期发送当前状态作为心跳，即使没有变化也发送
                dataSendSbus(lastSentState.forward, lastSentState.turn);
            }
        }, 100);
    }

    function stopHeartbeat() {
        if (heartbeatInterval) {
            clearInterval(heartbeatInterval);
            heartbeatInterval = null;
        }
    }

    function initControllers() {
        const keyboard = new KeyboardController({
            curve: DEFAULT_SPEED_CURVE,
            onVisualChange: (state) => {
                dataState.W = !!state.W?.pressed;
                dataState.S = !!state.S?.pressed;
                dataState.A = !!state.A?.pressed;
                dataState.D = !!state.D?.pressed;
                dataUpdateKeyVisual();
            },
        });

        const xbox = new XboxController({});

        controllerManager.register('keyboard', keyboard, 10);
        controllerManager.register('xbox', xbox, 8);

        // For desktop, register single joystick if elements exist
        if (dataElements.joystickContainer && dataElements.joystickBase && dataElements.joystickHandle) {
            const joystick = new VirtualJoystickController({
                elements: {
                    container: dataElements.joystickContainer,
                    base: dataElements.joystickBase,
                    handle: dataElements.joystickHandle,
                },
            });
            controllerManager.register('joystick', joystick, 5);
        }

        // Dual joystick is initialized separately in dual_joystick_init.js for mobile

        // Set up controller status change callback
        controllerManager.setControllerStatusCallback((statuses) => {
            updateControllerStatusIcons(statuses);
        });

        // Expose controllerManager for gyroscope controller access
        window.controllerManager = controllerManager;
    }

    function updateControllerStatusIcons(statuses) {
        // Update keyboard status
        updateStatusIcon(dataElements.keyboardStatus, statuses.keyboard);

        // Update xbox status
        updateStatusIcon(dataElements.xboxStatus, statuses.xbox);

        // Update gyroscope status
        updateStatusIcon(dataElements.gyroStatus, statuses.gyroscope || statuses.gyro);
    }

    function updateStatusIcon(element, active) {
        if (!element) return;
        if (active) {
            element.classList.add('active');
            element.classList.remove('inactive');
        } else {
            element.classList.remove('active');
            element.classList.add('inactive');
        }
    }

    function updateStatus(message) {
        dataStatusDiv.textContent = 'Status: ' + message;
        console.log('Status: ' + message);
    }

    // Connection UI helpers
    function dataToggleNoSignalOverlay(show) {
        const overlay = document.getElementById('noSignalOverlay');
        if (overlay) overlay.style.display = show ? 'flex' : 'none';
    }

    function dataUpdateConnStatus(kind, text) {
        if (!dataElements.connStatus) return;
        const color = kind === 'connected' ? 'dot-green' : kind === 'connecting' ? 'dot-yellow' : 'dot-red';
        dataElements.connStatus.innerHTML =
            '<span class="status-dot ' + color + '"></span><span class="mono">' + text + '</span>';
    }

    function dataUpdateKeyVisual() {
        if (dataElements.keyW) dataElements.keyW.classList.toggle('active', dataState.W);
        if (dataElements.keyA) dataElements.keyA.classList.toggle('active', dataState.A);
        if (dataElements.keyS) dataElements.keyS.classList.toggle('active', dataState.S);
        if (dataElements.keyD) dataElements.keyD.classList.toggle('active', dataState.D);
    }

    // 更新SBUS通道显示
    function dataUpdateChannelDisplay() {
        const values = window.currentChannelValues || [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

        for (let i = 0; i < 16; i++) {
            const channelNum = i + 1;
            const value = values[i] || 0;
            const fillElement = document.getElementById(`channel${channelNum}Fill`);
            const valueElement = document.getElementById(`channel${channelNum}Value`);

            if (fillElement && valueElement) {
                // 设置填充样式
                if (value > 0) {
                    fillElement.className = 'channel-bar-fill positive';
                    fillElement.style.height = `${value * 50}%`;
                    fillElement.style.bottom = '50%';
                    fillElement.style.top = 'auto';
                } else if (value < 0) {
                    fillElement.className = 'channel-bar-fill negative';
                    fillElement.style.height = `${Math.abs(value) * 50}%`;
                    fillElement.style.top = '50%';
                    fillElement.style.bottom = 'auto';
                } else {
                    fillElement.className = 'channel-bar-fill';
                    fillElement.style.height = '2px';
                    fillElement.style.top = '50%';
                    fillElement.style.bottom = 'auto';
                }

                // 更新数值显示
                valueElement.textContent = value.toFixed(2);
            }
        }
    }

    function dataUpdateSystemStatusDisplay() {
        const {rxSpeed, txSpeed, cpuUsage, ttyService, rtspService, signalStrength, sim_status, lastUpdate} =
            dataSystemStatus;

        if (dataElements.rxSpeed) dataElements.rxSpeed.textContent = `${rxSpeed.toFixed(2)} Kbit/s`;
        if (dataElements.txSpeed) dataElements.txSpeed.textContent = `${txSpeed.toFixed(2)} Kbit/s`;
        if (dataElements.cpuUsage) dataElements.cpuUsage.textContent = `${cpuUsage.toFixed(2)}%`;
        if (dataElements.serviceStatus)
            dataElements.serviceStatus.textContent = `🕹:${ttyService ? 'ON' : 'OFF'} / 🖼:${rtspService ? 'ON' : 'OFF'}`;
        if (dataElements.signalStrength)
            dataElements.signalStrength.textContent = signalStrength === -1 ? '-- dBm' : `${signalStrength} dBm`;
        if (dataElements.simStatus) dataElements.simStatus.textContent = sim_status;
        if (dataElements.speedValue) dataElements.speedValue.textContent = `${uiSpeed}`;
        if (dataElements.lastUpdate) dataElements.lastUpdate.textContent = lastUpdate ? lastUpdate.toLocaleTimeString() : '--';

        if (dataElements.signalIndicator) {
            const bars = dataElements.signalIndicator.querySelectorAll('.signal-bar');
            const level =
                signalStrength >= -75
                    ? 5
                    : signalStrength >= -85
                        ? 4
                        : signalStrength >= -95
                            ? 3
                            : signalStrength >= -105
                                ? 2
                                : signalStrength >= -115
                                    ? 1
                                    : 0;
            bars.forEach((bar, index) => {
                if (index < level) bar.classList.add('active');
                else bar.classList.remove('active');
            });
            if (dataElements.signalQuality) dataElements.signalQuality.textContent = `${level}/5`;
        }
    }

    function dataParseHexString(hexString) {
        hexString = hexString.replace(/\s+/g, '');
        if (hexString.length % 2 !== 0) return null;
        const bytes = new Uint8Array(hexString.length / 2);
        for (let i = 0; i < hexString.length; i += 2) {
            bytes[i / 2] = parseInt(hexString.substr(i, 2), 16);
        }
        return bytes;
    }

    function dataParseSystemStatusFrame(data) {
        try {
            // Convert data array to string directly (raw JSON)
            let dataStr = '';
            for (let i = 0; i < data.length; i++) {
                dataStr += String.fromCharCode(data[i]);
            }

            // Parse JSON directly
            const statusData = JSON.parse(dataStr);
            return statusData;
        } catch (error) {
            console.error('解析系统状态帧时出错:', error);
            return null;
        }
    }

    function dataHandleSystemStatusData(statusData) {
        if (!statusData) return;
        if (statusData.rx_speed !== undefined) dataSystemStatus.rxSpeed = (parseInt(statusData.rx_speed) * 8) / 100;
        if (statusData.tx_speed !== undefined) dataSystemStatus.txSpeed = (parseInt(statusData.tx_speed) * 8) / 100;
        if (statusData.cpu_usage !== undefined) dataSystemStatus.cpuUsage = parseInt(statusData.cpu_usage) / 100;
        if (statusData.tty_service !== undefined) dataSystemStatus.ttyService = statusData.tty_service === '1';
        if (statusData.rtsp_service !== undefined) dataSystemStatus.rtspService = statusData.rtsp_service === '1';
        if (statusData['4g_signal'] !== undefined) {
            const signalParts = statusData['4g_signal'].split(',');
            if (signalParts.length >= 1) dataSystemStatus.signalStrength = parseInt(signalParts[0]);
        }
        if (statusData.sim_status !== undefined) dataSystemStatus.sim_status = statusData.sim_status;
        dataSystemStatus.lastUpdate = new Date();
        dataUpdateSystemStatusDisplay();
        dataUpdateConnStatus('connected', 'CONNECTED');
    }

    function dataHandleThrottlePreset(ev) {
        const activeTag = document.activeElement?.tagName;
        if (activeTag === 'INPUT' || activeTag === 'TEXTAREA') return;
        const preset = dataThrottlePresets[ev.code];
        if (!preset) return;
        ev.preventDefault();
        dataSetThrottleLimit(preset.limit);
    }

    // Button bindings (neutralize SBUS)
    if (dataElements.reconnectBtn) dataElements.reconnectBtn.addEventListener('click', () => window.location.reload());
    if (dataElements.stopAllBtn) dataElements.stopAllBtn.addEventListener('click', () => dataSendSbus(0, 0));
    if (dataElements.emgBtn) dataElements.emgBtn.addEventListener('click', () => dataSendSbus(0, 0));
    if (dataElements.throttleBtn) dataElements.throttleBtn.addEventListener('click', () => dataSendSbus(0, 0));
    if (dataElements.reconnectBtnMobile)
        dataElements.reconnectBtnMobile.addEventListener('click', () => window.location.reload());
    if (dataElements.stopAllBtnMobile) dataElements.stopAllBtnMobile.addEventListener('click', () => dataSendSbus(0, 0));
    if (dataElements.emgBtnMobile) dataElements.emgBtnMobile.addEventListener('click', () => dataSendSbus(0, 0));
    if (dataElements.throttleBtnMobile)
        dataElements.throttleBtnMobile.addEventListener('click', () => dataSendSbus(0, 0));

    // 键盘 1/2/3/4 切换油门最大值
    window.addEventListener('keydown', dataHandleThrottlePreset);
    // 初始化显示
    if (dataElements.throttleLimitIndicator) dataElements.throttleLimitIndicator.textContent = '不限';

    // Initialize controllers
    initControllers();

    // Send peer_close on page unload
    window.addEventListener('beforeunload', () => {
        stopHeartbeat();
        dataStopAutoReconnect(); // 页面卸载时停止自动重连
        dataStopWsReconnect(); // 页面卸载时停止 WebSocket 重连
        dataSendPeerClose();
    });

    // Connect signaling
    console.log('Connecting to signaling...');
    dataOpenSignaling(dataUrl)
        .then((ws) => {
            updateStatus('Signaling connected');
            dataStopWsReconnect(); // WebSocket 连接成功，停止重连
            dataOfferId.disabled = false;
            dataOfferBtn.disabled = false;
            dataOfferBtn.onclick = () => dataOfferPeerConnection(ws, dataOfferId.value);
            if (dataOfferId.value) {
                setTimeout(() => {
                    dataUpdateConnStatus('connecting', 'CONNECTING');
                    dataOfferBtn.click();
                }, 1000);
            }
        })
        .catch((err) => {
            console.error(err);
            dataUpdateConnStatus('disconnected', 'DISCONNECTED');
            updateStatus('Signaling connection failed: ' + err.message);
        });

    function dataSendPeerClose() {
        if (!dataSignalingWs || dataSignalingWs.readyState !== WebSocket.OPEN) return;
        const peerIds = Object.keys(dataPeerConnectionMap);
        if (peerIds.length > 0) {
            try {
                peerIds.forEach((id) => {
                    dataSignalingWs.send(JSON.stringify({id, type: 'peer_close'}));
                });
            } catch (e) {
                console.error('Failed to send peer_close message:', e);
            }
        }
    }

    function dataOpenSignaling(url) {
        return new Promise((resolve, reject) => {
            const ws = new WebSocket(url);
            dataSignalingWs = ws;
            ws.onopen = () => {
                console.log('Signaling WebSocket connected');
                resolve(ws);
            };
            ws.onerror = () => reject(new Error('WebSocket error'));
            ws.onclose = () => {
                console.error('Signaling WebSocket disconnected');
                updateStatus('Signaling disconnected');
                // Try to send peer_close message before WebSocket fully closes
                // Note: This may not work if WebSocket is already closed, but we try anyway
                dataSendPeerClose();
                dataSignalingWs = null;
                // 启动 WebSocket 自动重连
                dataStartWsReconnect(url);
            };
            ws.onmessage = (e) => {
                if (typeof e.data !== 'string') return;
                const message = JSON.parse(e.data);
                const {id, type} = message;
                let pc = dataPeerConnectionMap[id];
                if (!pc) {
                    if (type !== 'offer') return;
                    pc = dataCreatePeerConnection(ws, id);
                }
                switch (type) {
                    case 'offer':
                    case 'answer':
                        pc.setRemoteDescription({sdp: message.description, type: message.type})
                            .then(() => {
                                if (type === 'offer') {
                                    updateStatus(`Creating answer for ${id}`);
                                    dataSendLocalDescription(ws, id, pc, 'answer');
                                }
                            })
                            .catch((err) => {
                                updateStatus(`Error setting remote ${type}: ${err.message}`);
                                console.error(`Error setting remote ${type}:`, err);
                            });
                        break;
                    case 'candidate':
                        pc.addIceCandidate({candidate: message.candidate, sdpMid: message.mid});
                        break;
                }
            };
        });
    }

    function dataOfferPeerConnection(ws, id) {
        if (!id) {
            alert('Please enter a remote ID');
            return;
        }
        const pc = dataCreatePeerConnection(ws, id);
        const dc = pc.createDataChannel('control');
        dataSetupDataChannel(dc, id);
        dataUpdateConnStatus('connecting', `CONNECTING TO ${id}`);
        dataSendLocalDescription(ws, id, pc, 'offer');
    }

    function dataCreatePeerConnection(ws, id) {
        const pc = new RTCPeerConnection(rtcConfig);
        pc.oniceconnectionstatechange = () => {
            if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
                dataUpdateConnStatus('connected', 'CONNECTED');
                // dataStopAutoReconnect(); // 连接成功，停止自动重连
            } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // 启动自动重连
                // dataStartAutoReconnect(ws, id);
            }
        };
        pc.onconnectionstatechange = () => {
            if (pc.connectionState === 'connected') {
                dataUpdateConnStatus('connected', 'CONNECTED');
                dataToggleNoSignalOverlay(false);
                dataStopAutoReconnect(); // 连接成功，停止自动重连
            } else if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected' || pc.connectionState === 'closed') {
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // 启动自动重连
                dataStartAutoReconnect(ws, id);
            }
        };
        pc.onicecandidate = (event) => {
            if (event.candidate) dataSendLocalCandidate(ws, id, event.candidate);
        };
        pc.ontrack = (e) => console.log('Received remote track:', e.track.kind, e.track.id, e.track.readyState);
        dataPeerConnectionMap[id] = pc;
        return pc;
    }

    function dataSetupDataChannel(dc, id) {
        dc.onopen = () => {
            updateStatus(`Data channel open with ${id}`);
            dataCurrentDataChannel = dc;
            dataUpdateConnStatus('connected', 'CONNECTED');
            dc.send(`Hello from ${dataLocalId}`);
            // 启动心跳机制
            startHeartbeat();
            dataStopAutoReconnect(); // 数据通道打开，停止自动重连
        };
        dc.onclose = () => {
            updateStatus(`Data channel closed with ${id}`);
            if (dataCurrentDataChannel === dc) {
                dataCurrentDataChannel = null;
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                // 停止心跳机制
                stopHeartbeat();
                // 启动自动重连
                if (dataSignalingWs) {
                    dataStartAutoReconnect(dataSignalingWs, id);
                }
            }
        };
        dc.onmessage = (ev) => {
            if (ev.data instanceof ArrayBuffer) {
                const data = new Uint8Array(ev.data);
                const statusData = dataParseSystemStatusFrame(data);
                if (statusData) dataHandleSystemStatusData(statusData);
            } else if (typeof ev.data === 'string') {
                if (ev.data.startsWith('Binary data:')) {
                    const binaryData = dataParseHexString(ev.data);
                    if (binaryData) {
                        const statusData = dataParseSystemStatusFrame(binaryData);
                        if (statusData) dataHandleSystemStatusData(statusData);
                    }
                } else {
                    console.log('Text message:', ev.data);
                }
            }
        };
        dataDataChannelMap[id] = dc;
        return dc;
    }

    // 自动重连功能：每秒发送 offer 尝试重新连接
    function dataStartAutoReconnect(ws, id) {
        // 如果已经在重连，先停止
        dataStopAutoReconnect();

        updateStatus(`Auto-reconnecting to ${id}...`);
        dataUpdateConnStatus('connecting', 'RECONNECTING');

        dataReconnectInterval = setInterval(() => {
            // 检查 signaling 连接是否正常
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                console.log('Signaling connection lost, cannot reconnect');
                return;
            }

            // 清理旧的连接
            if (dataPeerConnectionMap[id]) {
                dataPeerConnectionMap[id].close();
                delete dataPeerConnectionMap[id];
            }

            // 创建新的 PeerConnection 并发送 offer
            dataOfferPeerConnection(ws, id);
        }, 1000); // 每秒重试一次
    }

    function dataStopAutoReconnect() {
        if (dataReconnectInterval) {
            clearInterval(dataReconnectInterval);
            dataReconnectInterval = null;
            console.log('Data auto-reconnect stopped');
        }
    }

    // WebSocket 自动重连功能
    function dataStartWsReconnect(url) {
        // 如果已经在重连，先停止
        dataStopWsReconnect();

        updateStatus('Reconnecting to signaling server...');
        console.log('Starting WebSocket reconnection to:', url);

        dataWsReconnectInterval = setInterval(() => {
            console.log('Attempting to reconnect to signaling server...');
            dataOpenSignaling(url)
                .then((ws) => {
                    console.log('Signaling server reconnected');
                    dataStopWsReconnect();
                    dataOfferId.disabled = false;
                    dataOfferBtn.disabled = false;
                    dataOfferBtn.onclick = () => dataOfferPeerConnection(ws, dataOfferId.value);
                    // 如果有远程 ID，自动尝试连接
                    if (dataOfferId.value) {
                        setTimeout(() => {
                            dataUpdateConnStatus('connecting', 'CONNECTING');
                            dataOfferPeerConnection(ws, dataOfferId.value);
                        }, 1000);
                    }
                })
                .catch((err) => {
                    console.error('Signaling reconnection failed:', err.message);
                });
        }, 3000); // 每 3 秒重试一次
    }

    function dataStopWsReconnect() {
        if (dataWsReconnectInterval) {
            clearInterval(dataWsReconnectInterval);
            dataWsReconnectInterval = null;
            console.log('WebSocket auto-reconnect stopped');
        }
    }

    function dataSendLocalDescription(ws, id, pc, type) {
        const options = type === 'offer' ? {offerToReceiveAudio: true, offerToReceiveVideo: true} : {};
        (type === 'offer' ? pc.createOffer(options) : pc.createAnswer())
            .then((desc) => pc.setLocalDescription(desc))
            .then(() => {
                const {sdp, type} = pc.localDescription;
                ws.send(JSON.stringify({id, type, description: sdp}));
            })
            .catch((err) => {
                console.error(`Error creating ${type}:`, err);
                updateStatus(`Error creating ${type}: ${err.message}`);
            });
    }

    function dataSendLocalCandidate(ws, id, cand) {
        const {candidate, sdpMid} = cand;
        ws.send(JSON.stringify({id, type: 'candidate', candidate, mid: sdpMid}));
    }

    function dataRandomId(length) {
        const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
        return [...Array(length)].map(pickRandom).join('');
    }
});

