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

        rxSpeed: document.getElementById('rxSpeed'),
        txSpeed: document.getElementById('txSpeed'),
        txSpeedUnit: document.getElementById('txSpeedUnit'),
        rxSpeedUnit: document.getElementById('rxSpeedUnit'),
        cpuValue: document.getElementById('cpuValue'),
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
        vehicleSpeed: 0,
        lastUpdate: null,
    };

    let uiSpeed = 0;
    let lastSentState = { forward: 0, turn: 0 };
    let actualSentForward = 0; // 实际发送的油门值（经过限幅）
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
        actualSentForward = limitedForward; // 保存实际发送的油门值
        uiSpeed = Math.round(Math.abs(limitedForward) * 100);
        dataUpdateSystemStatusDisplay();

        // 获取通道绑定的值
        let channelValues = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        if (window.channelKeyBinder) {
            channelValues = window.channelKeyBinder.getAllChannelValues();
        }

        try {
            // 使用新协议 RCProtocol v2，直接传输-1.0~1.0浮点数
            // CH1, CH2 由控制器控制，其他通道由按键绑定控制
            const frame = RCProtocol.encode({
                ch1: limitedForward,
                ch2: turn || 0,
                ch3: channelValues[2],
                ch4: channelValues[3],
                ch5: channelValues[4],
                ch6: channelValues[5],
                ch7: channelValues[6],
                ch8: channelValues[7],
                ch9: channelValues[8],
                ch10: channelValues[9],
                ch11: channelValues[10],
                ch12: channelValues[11],
                ch13: channelValues[12],
                ch14: channelValues[13],
                ch15: channelValues[14],
                ch16: channelValues[15]
            });
            dataCurrentDataChannel.send(frame);
            // 更新通道显示
            dataUpdateChannelDisplay();
        } catch (e) {
            console.error('Failed to send RC frame', e);
        }
    }

    // 启动心跳机制：定期发送心跳包，保持DataChannel活跃
    // 心跳包只更新心跳时间，不调用电机控制
    function startHeartbeat() {
        if (heartbeatInterval) clearInterval(heartbeatInterval);
        heartbeatInterval = setInterval(() => {
            if (dataCurrentDataChannel && dataCurrentDataChannel.readyState === 'open') {
                // 发送心跳包，不控制电机
                try {
                    const heartbeatFrame = RCProtocol.encodeHeartbeat();
                    dataCurrentDataChannel.send(heartbeatFrame);
                } catch (e) {
                    console.error('Failed to send heartbeat', e);
                }
            }
        }, 300);
    }

    function stopHeartbeat() {
        if (heartbeatInterval) {
            clearInterval(heartbeatInterval);
            heartbeatInterval = null;
        }
    }

    function initControllers() {
        // Initialize curve manager and load saved curve
        const currentSpeedCurve = speedCurveManager.getCurrentSpeedCurve() || DEFAULT_SPEED_CURVE;
        const currentCurve = speedCurveManager.getCurrentCurve();
        const curveName = currentCurve ? currentCurve.name : 'Default';
        console.log('Loading speed curve:', speedCurveManager.currentCurveId, curveName);
        updateStatus(`加速曲线: ${curveName}`);

        const keyboard = new KeyboardController({
            curve: currentSpeedCurve,
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

        // Set up curve change callback - update keyboard curve when it changes
        speedCurveManager.setOnChange((curve) => {
            if (curve) {
                // Update the keyboard controller's curve
                keyboard.curve = new SpeedCurve(curve.points);
                console.log('Speed curve updated:', curve.name);
                if (typeof updateStatus === 'function') {
                    updateStatus(`加速曲线已切换: ${curve.name}`);
                }
            }
        });

        // Listen for localStorage changes from curve editor page
        window.addEventListener('storage', (event) => {
            if (event.key === 'speedCurveId' || event.key === 'customSpeedCurves') {
                console.log('[Storage] Detected curve change:', event.key);
                // Reload custom curves from localStorage
                speedCurveManager.reloadCustomCurves();
                // Get the current curve
                const currentCurveId = localStorage.getItem('speedCurveId') || 'linear';
                const curve = speedCurveManager.getCurve(currentCurveId);
                if (curve) {
                    // Update the keyboard controller's curve
                    keyboard.curve = new SpeedCurve(curve.points);
                    console.log('[Storage] Speed curve reloaded:', curve.name);
                    if (typeof updateStatus === 'function') {
                        updateStatus(`加速曲线已更新: ${curve.name}`);
                    }
                }
            }
        });

        // Expose keyboard for curve updates
        window.keyboardController = keyboard;

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
        // 如果没有通道绑定器，使用默认值
        let values = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        if (window.currentChannelValues) {
            values = window.currentChannelValues;
        } else if (window.channelKeyBinder) {
            values = window.channelKeyBinder.getAllChannelValues();
        }

        // 更新 CH1 和 CH2 为当前的控制器值
        values[0] = lastSentState.forward || 0;
        values[1] = lastSentState.turn || 0;

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

    function formatSpeed(speed) {
        // speed 已经是 Kbps 单位
        if (speed >= 1000000) {
            const value = speed / 1000000;
            return { value: value >= 100 ? Math.round(value).toString() : value.toFixed(1), unit: 'Gbps' };
        } else if (speed >= 1000) {
            const value = speed / 1000;
            return { value: value >= 100 ? Math.round(value).toString() : value.toFixed(1), unit: 'Mbps' };
        } else {
            const value = speed;
            return { value: value >= 100 ? Math.round(value).toString() : value.toFixed(1), unit: 'Kbps' };
        }
    }

    function dataUpdateSystemStatusDisplay() {
        const {rxSpeed, txSpeed, cpuUsage, lastUpdate} =
            dataSystemStatus;

        if (dataElements.rxSpeed && dataElements.rxSpeedUnit) {
            const rxSpeedFormatted = formatSpeed(rxSpeed);
            dataElements.rxSpeed.textContent = rxSpeedFormatted.value;
            dataElements.rxSpeedUnit.textContent = rxSpeedFormatted.unit;
        }
        if (dataElements.txSpeed && dataElements.txSpeedUnit) {
            const txSpeedFormatted = formatSpeed(txSpeed);
            dataElements.txSpeed.textContent = txSpeedFormatted.value;
            dataElements.txSpeedUnit.textContent = txSpeedFormatted.unit;
        }
        
        // 更新CPU仪表盘
        updateCpuGauge(cpuUsage);

        // 更新油门比例仪表盘 (使用实际发送的油门值)
        const throttlePercent = Math.abs(actualSentForward * 100);
        updateThrottleGauge(throttlePercent);

        // 更新车辆速度表盘 (使用statusData中的vehicleSpeed)
        updateVehicleSpeedGauge(dataSystemStatus.vehicleSpeed);
        
        if (dataElements.lastUpdate) dataElements.lastUpdate.textContent = lastUpdate ? lastUpdate.toLocaleTimeString() : '--';
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
        if (statusData.vehicle_speed !== undefined) dataSystemStatus.vehicleSpeed = parseFloat(statusData.vehicle_speed);
        dataSystemStatus.lastUpdate = new Date();
        dataUpdateSystemStatusDisplay();
        dataUpdateConnStatus('connected', 'CONNECTED');
    }

    function updateThrottleGauge(throttlePercent) {
        const throttleProgress = document.getElementById('throttleGaugeProgress');
        const throttleValue = document.getElementById('throttleValue');
        
        if (throttleProgress && throttleValue) {
            // 仪表盘弧的总长度是 251.2 (2 * PI * 40 * 0.8)
            const maxDash = 251.2;
            const percent = Math.max(0, Math.min(100, throttlePercent));
            const dashOffset = maxDash - (percent / 100) * maxDash;
            
            throttleProgress.style.strokeDashoffset = dashOffset;
            throttleValue.textContent = Math.round(percent);
            
            // 根据百分比改变颜色
            if (percent >= 80) {
                throttleProgress.style.stroke = '#FF4500';
            } else if (percent >= 50) {
                throttleProgress.style.stroke = '#FFA500';
            } else {
                throttleProgress.style.stroke = '#32CD32';
            }
        }
    }

    function updateVehicleSpeedGauge(speed) {
        const speedPointer = document.getElementById('speedPointer');
        const pointerGroup = speedPointer?.closest('.pointer-group');
        const vehicleSpeedValue = document.getElementById('vehicleSpeedValue');

        if (speedPointer && pointerGroup && vehicleSpeedValue) {
            // 最大速度为 160 km/h
            const maxSpeed = 160;
            const clampedSpeed = Math.max(0, Math.min(maxSpeed, speed));

            // 角度范围: 从 -120度 到 120度 (共240度)
            const angle = -120 + (clampedSpeed / maxSpeed) * 240;
            pointerGroup.style.transform = `rotate(${angle}deg)`;

            vehicleSpeedValue.textContent = Math.round(clampedSpeed);
        }
    }

    function updateCpuGauge(cpuPercent) {
        const cpuProgress = document.getElementById('cpuProgress');
        const cpuValue = document.getElementById('cpuValue');
        
        if (cpuProgress && cpuValue) {
            // 圆周长是 283 (2 * PI * 45)
            const maxDash = 283;
            const percent = Math.max(0, Math.min(100, cpuPercent));
            const dashOffset = maxDash - (percent / 100) * maxDash;
            
            cpuProgress.style.strokeDashoffset = dashOffset;
            cpuValue.textContent = Math.round(percent);
            
            // 根据CPU使用率改变颜色
            if (percent >= 80) {
                cpuProgress.style.stroke = '#FF4500';
            } else if (percent >= 50) {
                cpuProgress.style.stroke = '#FFA500';
            } else {
                cpuProgress.style.stroke = '#00CED1';
            }
        }
    }

    // 初始化速度表盘刻度
    function initSpeedGaugeTicks() {
        const speedTicksGroup = document.getElementById('speedTicks');
        const speedNumbersGroup = document.getElementById('speedNumbers');
        
        if (!speedTicksGroup || !speedNumbersGroup) return;
        
        const maxSpeed = 160;
        const centerX = 150;
        const centerY = 150;
        const radius = 100;
        
        // 生成刻度 (每10km/h一个主刻度,每20km/h一个数字)
        for (let speed = 0; speed <= maxSpeed; speed += 10) {
            const angle = -120 + (speed / maxSpeed) * 240;
            const angleRad = (angle * Math.PI) / 180;
            
            const isMajor = speed % 20 === 0;
            const length = isMajor ? 12 : 8;
            const strokeWidth = isMajor ? 2 : 1;
            
            const innerX = centerX + (radius - length) * Math.sin(angleRad);
            const innerY = centerY - (radius - length) * Math.cos(angleRad);
            const outerX = centerX + radius * Math.sin(angleRad);
            const outerY = centerY - radius * Math.cos(angleRad);
            
            const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
            line.setAttribute('x1', innerX);
            line.setAttribute('y1', innerY);
            line.setAttribute('x2', outerX);
            line.setAttribute('y2', outerY);
            line.setAttribute('stroke', speed >= 120 ? '#FF4500' : '#fff');
            line.setAttribute('stroke-width', strokeWidth);
            speedTicksGroup.appendChild(line);
            
            // 每20km/h显示数字
            if (isMajor && speed > 0) {
                const textRadius = radius - 25;
                const textX = centerX + textRadius * Math.sin(angleRad);
                const textY = centerY - textRadius * Math.cos(angleRad);
                
                const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
                text.setAttribute('x', textX);
                text.setAttribute('y', textY);
                text.setAttribute('text-anchor', 'middle');
                text.setAttribute('dominant-baseline', 'middle');
                text.setAttribute('fill', speed >= 120 ? '#FF4500' : '#fff');
                text.setAttribute('font-size', '12');
                text.setAttribute('font-weight', '600');
                text.textContent = speed;
                speedNumbersGroup.appendChild(text);
            }
        }
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


    // 初始化速度表盘刻度
    initSpeedGaugeTicks();

    // 初始化速度表盘指针到0位置
    updateVehicleSpeedGauge(0);

    // 初始化油门比例仪表盘到0%
    updateThrottleGauge(0);

    // 初始化通道按键绑定器
    if (typeof ChannelKeyBinder !== 'undefined') {
        window.channelKeyBinder = new ChannelKeyBinder();
        window.channelKeyBinder.loadFromConfig();
        window.channelKeyBinder.start();

        // 当通道值变化时，立即发送新的 SBUS 帧
        window.channelKeyBinder.setOnValueChange((values) => {
            if (lastSentState) {
                // 使用最后发送的 forward 和 turn 值
                dataSendSbus(lastSentState.forward, lastSentState.turn);
            }
        });

        console.log('通道按键绑定器已启动');
    }

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

