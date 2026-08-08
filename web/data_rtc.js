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
    let dataIsReconnecting = false; // 全局标记：是否正在重连中（用于避免并发连接）

    // 更新 DATA LINK 的 ICE 信息展示
    function dataUpdateIceInfoDisplay(id) {
        const pc = dataPeerConnectionMap[id];
        if (!pc) return;

        // 更新 ICE 状态
        const iceStatusEl = document.getElementById('dataIceStatus');
        if (iceStatusEl) {
            iceStatusEl.textContent = pc.iceConnectionState || '--';
        }

        // 获取并展示选中的 ICE 候选对
        if (pc.connectionState === 'connected' || pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
            pc.getStats().then(stats => {
                let foundPair = false;
                stats.forEach((report) => {
                    // 查找选中的候选对（selected 或 state 为 succeeded）
                    if (report.type === 'candidate-pair' && (report.selected || report.state === 'succeeded')) {
                        foundPair = true;
                        const localCandidateId = report.localCandidateId;
                        const remoteCandidateId = report.remoteCandidateId;

                        if (localCandidateId && remoteCandidateId) {
                            const localCandidate = stats.get(localCandidateId);
                            const remoteCandidate = stats.get(remoteCandidateId);
                            
                            if (localCandidate && remoteCandidate) {
                                // 计算RTT（在更大的作用域内定义，方便后面使用）
                                const rttText = report.currentRoundTripTime ? (report.currentRoundTripTime * 1000).toFixed(0) + 'ms' : '--';
                                const rttMs = report.currentRoundTripTime ? report.currentRoundTripTime * 1000 : 0;

                                // 更新连接类型
                                const connectionTypeEl = document.getElementById('dataConnectionType');
                                if (connectionTypeEl) {
                                    const localType = localCandidate.candidateType || 'unknown';
                                    const isRelay = localType === 'relay';
                                    connectionTypeEl.textContent = isRelay ? 'TURN (Relay)' : 'P2P (Direct)';
                                    connectionTypeEl.style.color = isRelay ? '#FFA500' : '#32CD32';
                                }

                                // 更新本地候选（尝试多种可能的字段名）
                                const localCandidateEl = document.getElementById('dataLocalCandidate');
                                if (localCandidateEl) {
                                    const address = localCandidate.address 
                                        || localCandidate.ip 
                                        || localCandidate.ipAddress 
                                        || '(hidden)';
                                    const port = localCandidate.port || '--';
                                    const protocol = localCandidate.protocol || '--';
                                    const candidateType = localCandidate.candidateType || 'unknown';
                                    localCandidateEl.textContent = `${address}:${port} (${protocol})`;
                                    localCandidateEl.title = `Type: ${candidateType}\nPriority: ${localCandidate.priority || '--'}\nFoundation: ${localCandidate.foundation || '--'}\nRTT: ${rttText}`;
                                }

                                // 更新RTT显示
                                const rttEl = document.getElementById('dataRtt');
                                if (rttEl) {
                                    rttEl.textContent = rttText;
                                    if (rttMs > 150) {
                                        rttEl.style.color = '#FF4500'; // 高延迟：红色
                                    } else if (rttMs > 50) {
                                        rttEl.style.color = '#FFA500'; // 中等延迟：橙色
                                    } else {
                                        rttEl.style.color = '#32CD32'; // 低延迟：绿色
                                    }
                                }

                                // 更新远程候选（尝试多种可能的字段名）
                                const remoteCandidateEl = document.getElementById('dataRemoteCandidate');
                                if (remoteCandidateEl) {
                                    const address = remoteCandidate.address 
                                        || remoteCandidate.ip 
                                        || remoteCandidate.ipAddress 
                                        || '(hidden)';
                                    const port = remoteCandidate.port || '--';
                                    const protocol = remoteCandidate.protocol || '--';
                                    const candidateType = remoteCandidate.candidateType || 'unknown';
                                    remoteCandidateEl.textContent = `${address}:${port} (${protocol})`;
                                    remoteCandidateEl.title = `Type: ${candidateType}\nPriority: ${remoteCandidate.priority || '--'}\nFoundation: ${remoteCandidate.foundation || '--'}`;
                                }
                            }
                        }
                    }
                });
                if (!foundPair) {
                    console.warn('No selected ICE candidate pair found');
                }
            }).catch(err => {
                console.warn('Failed to get ICE stats:', err);
            });
        }
    }

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
        batteryValue: document.getElementById('batteryValue'),
        batteryBar: document.getElementById('batteryBar'),
        batteryIcon: document.getElementById('batteryIcon'),
        batteryChargeIcon: document.getElementById('batteryChargeIcon'),
    };

    // System status snapshot
    const dataSystemStatus = {
        rxSpeed: 0,
        txSpeed: 0,
        cpuUsage: 0,
        memTotal: 0,
        memUsed: 0,
        connectionCount: 0,
        vehicleSpeed: 0,
        lastUpdate: null,
        // 电池数据
        batteryLevel: -1,        // 电量百分比 (-1 表示未收到)
        batteryCharging: false,  // 是否充电中
        batteryReceived: false,  // 是否已收到过电池数据
        // GPS data
        gpsLatitude: 0,
        gpsLongitude: 0,
        gpsAltitude: 0,
        gpsQuality: 0,
        gpsSatellites: 0,
        homeLatitude: null,
        homeLongitude: null,
        // IMU data for trajectory
        accelAx: 0,
        accelAy: 0,
        accelAz: 0,
        attitudePitch: 0,
        attitudeRoll: 0,
        attitudeYaw: 0,
        hasImuHeading: false,   // true when IMU yaw data has been received
    };

    let uiSpeed = 0;
    let lastSentState = { forward: 0, turn: 0 };
    let actualSentForward = 0; // 实际发送的油门值（经过限幅）
    // let heartbeatInterval = null;
    let controlInterval = null; // 定时发送控制帧，确保长按时持续发送

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
        // 始终更新通道显示（即使未连接也要反映当前值）
        dataUpdateChannelDisplay();

        if (!dataCurrentDataChannel || dataCurrentDataChannel.readyState !== 'open') return;
        const limitedForward = dataApplyThrottleLimit(forward);
        actualSentForward = limitedForward;
        uiSpeed = Math.round(Math.abs(limitedForward) * 100);
        lastSentState = { forward: limitedForward, turn: turn || 0 };
        dataUpdateSystemStatusDisplay();

        // ChannelKeyBinder 是全部16个通道的唯一数据源（包括 CH1/CH2）
        let channelValues = [1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500];
        if (window.channelKeyBinder) {
            channelValues = window.channelKeyBinder.getAllChannelValues();
        }

        // CH1/CH2：如果 ChannelKeyBinder 已绑定（含曲线），使用其计算值；
        // 否则用 ControllerManager 的 forward/turn 线性映射
        if (!window.channelKeyBinder || !window.channelKeyBinder.getBinding(1)) {
            const rawForward = 1500 + (lastSentState.forward || 0) * 500;
            channelValues[0] = Math.max(1000, Math.min(2000, rawForward));
        }
        if (!window.channelKeyBinder || !window.channelKeyBinder.getBinding(2)) {
            const rawTurn = 1500 + (lastSentState.turn || 0) * 500;
            channelValues[1] = Math.max(1000, Math.min(2000, rawTurn));
        }

        try {
            const frame = RCProtocol.encode({
                ch1: channelValues[0],
                ch2: channelValues[1],
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
            dataUpdateChannelDisplay();
        } catch (e) {
            console.error('Failed to send RC frame', e);
        }
    }

    /*
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
    */

    // 定时发送控制帧：只在有实际操作时持续发送（非中位），确保长按时数据不中断
    function startControlLoop() {
        if (controlInterval) clearInterval(controlInterval);
        controlInterval = setInterval(() => {
            if (dataCurrentDataChannel && dataCurrentDataChannel.readyState === 'open') {
                // 只有非中位（有操作）时才发送控制帧
                if (lastSentState.forward !== 0 || lastSentState.turn !== 0) {
                    dataSendSbus(lastSentState.forward, lastSentState.turn);
                }
            }
        }, 50); // 20Hz
    }

    function stopControlLoop() {
        if (controlInterval) {
            clearInterval(controlInterval);
            controlInterval = null;
        }
    }

    function initControllers() {
        // KeyboardController 仅用于 WASD 按键视觉反馈（通道条高亮）
        // 实际数据由 ChannelKeyBinder 统一管理所有16个通道
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

        // 监听曲线编辑器保存，刷新 binder 曲线缓存
        window.addEventListener('storage', (event) => {
            if (event.key === 'customSpeedCurves' && window.channelKeyBinder) {
                speedCurveManager.reloadCustomCurves();
                window.channelKeyBinder.refreshCurves();
                console.log('[Storage] Channel binder curves refreshed from editor');
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

    // 更新SBUS通道显示（全部来自 ChannelKeyBinder）
    function dataUpdateChannelDisplay() {
        let values = [1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500];
        if (window.currentChannelValues) {
            values = window.currentChannelValues;
        } else if (window.channelKeyBinder) {
            values = window.channelKeyBinder.getAllChannelValues();
        }

        for (let i = 0; i < 16; i++) {
            const channelNum = i + 1;
            const value = values[i] || 1500;
            const fillElement = document.getElementById(`channel${channelNum}Fill`);
            const valueElement = document.getElementById(`channel${channelNum}Value`);

            // 获取该通道的 neutralValue（从 binding 中读取，默认 1500）
            let neutralValue = 1500;
            if (window.channelKeyBinder) {
                const binding = window.channelKeyBinder.getBinding(channelNum);
                if (binding && binding.neutralValue !== undefined) {
                    neutralValue = binding.neutralValue;
                }
            }

            if (fillElement && valueElement) {
                // 以 neutralValue 为中心显示
                const diff = value - neutralValue;
                const range = 500; // 1000~2000 范围半宽

                if (diff > 0.5) {
                    fillElement.className = 'channel-bar-fill positive';
                    fillElement.style.height = `${Math.min(diff / range * 50, 50)}%`;
                    fillElement.style.bottom = '50%';
                    fillElement.style.top = 'auto';
                } else if (diff < -0.5) {
                    fillElement.className = 'channel-bar-fill negative';
                    fillElement.style.height = `${Math.min(Math.abs(diff) / range * 50, 50)}%`;
                    fillElement.style.top = '50%';
                    fillElement.style.bottom = 'auto';
                } else {
                    fillElement.className = 'channel-bar-fill';
                    fillElement.style.height = '2px';
                    fillElement.style.top = '50%';
                    fillElement.style.bottom = 'auto';
                }

                // 显示整数 raw PWM 值
                valueElement.textContent = value.toFixed(0);
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
        const {rxSpeed, txSpeed, lastUpdate} = dataSystemStatus;

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

        // 更新系统状态面板
        updateSystemStatusPanel();

        // 更新油门比例仪表盘 (使用实际发送的油门值)
        const throttlePercent = Math.abs(actualSentForward * 100);
        updateThrottleGauge(throttlePercent);

        // 更新车辆速度表盘 (使用statusData中的vehicleSpeed)
        updateVehicleSpeedGauge(dataSystemStatus.vehicleSpeed);

        // 更新GPS雷达（包含卫星数量更新）
        updateGpsRadar();

        if (dataElements.lastUpdate) dataElements.lastUpdate.textContent = lastUpdate ? lastUpdate.toLocaleTimeString() : '--';
    }

    function updateSystemStatusPanel() {
        const {cpuUsage, memUsed, memTotal, connectionCount} = dataSystemStatus;

        // 更新CPU状态 - 显示百分比数值
        updateStatusItem('cpu', cpuUsage, cpuUsage, '%', 80);

        // 更新内存状态 - 超过1024MB显示GB
        const memPercent = memTotal > 0 ? (memUsed / memTotal) * 100 : 0;
        let memValueText = '0 MB';
        if (memUsed > 0) {
            if (memUsed >= 1024) {
                memValueText = `${(memUsed / 1024).toFixed(2)} GB`;
            } else {
                memValueText = `${memUsed} MB`;
            }
        }
        updateStatusItem('mem', memPercent, memValueText, 90);

        // 更新连接数
        const connCountElement = document.getElementById('connCount');
        const connStatusItem = connCountElement?.closest('.status-item');
        if (connCountElement) {
            connCountElement.textContent = connectionCount || 0;
        }
        if (connStatusItem) {
            connStatusItem.style.display = (connectionCount > 0) ? '' : 'none';
        }

        // 更新电池状态（电量百分比 + 充电图标）
        updateBatteryPanel();
    }

    function updateBatteryPanel() {
        if (!dataSystemStatus.batteryReceived) return; // 未收到过电池数据则不显示

        const {batteryLevel, batteryCharging} = dataSystemStatus;
        const batteryItem = dataElements.batteryValue?.closest('.status-item');

        // 显示电池块
        if (batteryItem) batteryItem.style.display = '';

        // 电量数值
        if (dataElements.batteryValue) {
            dataElements.batteryValue.textContent = `${Math.max(0, Math.min(100, batteryLevel))}%`;
        }
        // 电量进度条
        if (dataElements.batteryBar) {
            dataElements.batteryBar.style.width = `${Math.max(0, Math.min(100, batteryLevel))}%`;
            // 低电量变红, 充电时变绿
            if (batteryCharging) {
                dataElements.batteryBar.style.background = '#32CD32';
            } else if (batteryLevel <= 20) {
                dataElements.batteryBar.style.background = '#FF4500';
            } else {
                dataElements.batteryBar.style.background = '';
            }
        }
        // 充电图标切换
        if (dataElements.batteryChargeIcon && dataElements.batteryIcon) {
            dataElements.batteryChargeIcon.style.display = batteryCharging ? '' : 'none';
            dataElements.batteryIcon.style.display = batteryCharging ? 'none' : '';
        }
    }

    function updateStatusItem(type, value, displayValue, suffix, highLoadThreshold) {
        const percent = Math.max(0, Math.min(100, value));

        const valueElement = document.getElementById(`${type}Value`);
        const barElement = document.getElementById(`${type}Bar`);

        // 值为0时隐藏整个面板
        const statusItem = valueElement?.closest('.status-item');
        if (statusItem) {
            statusItem.style.display = (percent > 0) ? '' : 'none';
        }
        if (!statusItem || percent <= 0) return;

        if (valueElement) {
            if (typeof displayValue === 'number') {
                valueElement.textContent = `${Math.round(displayValue)}${suffix || ''}`;
            } else {
                valueElement.textContent = displayValue;
            }
        }

        if (barElement) {
            barElement.style.width = `${percent}%`;

            // 根据负载改变颜色
            if (statusItem) {
                if (percent >= highLoadThreshold) {
                    statusItem.classList.add('high-load');
                } else {
                    statusItem.classList.remove('high-load');
                }
            }
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
        // console.log("Data: ", statusData)
        if (!statusData) return;
        if (statusData.rx_speed !== undefined) dataSystemStatus.rxSpeed = (parseInt(statusData.rx_speed) * 8) / 100;
        if (statusData.tx_speed !== undefined) dataSystemStatus.txSpeed = (parseInt(statusData.tx_speed) * 8) / 100;

        // CPU信息
        if (statusData.cpu_usage !== undefined) {
            dataSystemStatus.cpuUsage = parseInt(statusData.cpu_usage) / 100;
        }

        // 内存信息
        if (statusData.mem_total_mb !== undefined) {
            dataSystemStatus.memTotal = parseInt(statusData.mem_total_mb);
        }
        if (statusData.mem_used_mb !== undefined) {
            dataSystemStatus.memUsed = parseInt(statusData.mem_used_mb);
        }

        // 电池信息
        if (statusData.battery_level !== undefined) {
            dataSystemStatus.batteryLevel = parseInt(statusData.battery_level);
            dataSystemStatus.batteryReceived = true;
        }
        if (statusData.battery_charging !== undefined) {
            dataSystemStatus.batteryCharging = (statusData.battery_charging === '1'
                || statusData.battery_charging === 1
                || statusData.battery_charging === true
                || statusData.battery_charging === 'true');
            dataSystemStatus.batteryReceived = true;
        }

        // 连接数
        if (statusData.connection_count !== undefined) {
            dataSystemStatus.connectionCount = parseInt(statusData.connection_count);
        }

        // ---- IMU 数据（轨迹计算 / 水平方向） ----
        if (statusData.accel_ax !== undefined) dataSystemStatus.accelAx = parseFloat(statusData.accel_ax);
        if (statusData.accel_ay !== undefined) dataSystemStatus.accelAy = parseFloat(statusData.accel_ay);
        if (statusData.accel_az !== undefined) dataSystemStatus.accelAz = parseFloat(statusData.accel_az);
        if (statusData.attitude_pitch !== undefined) dataSystemStatus.attitudePitch = parseFloat(statusData.attitude_pitch);
        if (statusData.attitude_roll !== undefined) dataSystemStatus.attitudeRoll = parseFloat(statusData.attitude_roll);
        if (statusData.attitude_yaw !== undefined) {
            dataSystemStatus.attitudeYaw = parseFloat(statusData.attitude_yaw);
            dataSystemStatus.hasImuHeading = true;   // 标记已收到 IMU 航向数据
        }

        // 车辆速度
        if (statusData.gps_speed_kmh !== undefined) {
            dataSystemStatus.vehicleSpeed = parseFloat(statusData.gps_speed_kmh);
        }

        // GPS数据
        if (statusData.gps_latitude !== undefined) {
            dataSystemStatus.gpsLatitude = parseFloat(statusData.gps_latitude);
        }
        if (statusData.gps_longitude !== undefined) {
            dataSystemStatus.gpsLongitude = parseFloat(statusData.gps_longitude);
        }
        if (statusData.gps_altitude !== undefined) {
            dataSystemStatus.gpsAltitude = parseFloat(statusData.gps_altitude);
        }
        if (statusData.gps_quality !== undefined) {
            dataSystemStatus.gpsQuality = parseInt(statusData.gps_quality);
        }
        if (statusData.gps_satellites !== undefined) {
            dataSystemStatus.gpsSatellites = parseInt(statusData.gps_satellites);
        }

        // 第一次收到有效GPS数据时，设置为起始位置
        if (dataSystemStatus.homeLatitude === null &&
            dataSystemStatus.gpsLatitude !== 0 &&
            dataSystemStatus.gpsLongitude !== 0 &&
            dataSystemStatus.gpsSatellites >= 4) { // 至少4颗卫星才有效
            dataSystemStatus.homeLatitude = dataSystemStatus.gpsLatitude;
            dataSystemStatus.homeLongitude = dataSystemStatus.gpsLongitude;
            console.log('Home position set:', dataSystemStatus.homeLatitude, dataSystemStatus.homeLongitude);
        }

        dataSystemStatus.lastUpdate = new Date();
        dataUpdateSystemStatusDisplay();
        dataUpdateConnStatus('connected', 'CONNECTED');
    }

    function updateThrottleGauge(throttlePercent) {
        const ringFill = document.getElementById('throttleRingFill');
        const throttleValue = document.getElementById('throttleValue');
        
        if (ringFill && throttleValue) {
            const percent = Math.max(0, Math.min(100, throttlePercent));
            // 圆周长 = 2 * PI * 60 ≈ 376.99
            const circumference = 376.99;
            const dashOffset = circumference - (percent / 100) * circumference;
            
            ringFill.style.strokeDashoffset = dashOffset;
            throttleValue.textContent = Math.round(percent) + '%';
            
            // 根据百分比改变颜色
            let color, glow;
            if (percent >= 80) {
                color = '#ef4444';
                glow = '0 0 8px rgba(239, 68, 68, 0.6)';
            } else if (percent >= 50) {
                color = '#f97316';
                glow = '0 0 8px rgba(249, 115, 22, 0.6)';
            } else {
                color = '#22c55e';
                glow = '0 0 8px rgba(34, 197, 94, 0.6)';
            }
            ringFill.style.stroke = color;
            throttleValue.style.color = color;
            throttleValue.style.textShadow = glow;
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

    function updateGpsRadar() {
        const radarArrow = document.getElementById('radarArrow');
        const radarHome = document.getElementById('radarHome');
        const satelliteCount = document.getElementById('satelliteCount');
        const satelliteIndicator = document.getElementById('satelliteIndicator');

        if (!radarArrow || !radarHome) return;

        // 更新卫星数量显示
        if (satelliteCount) {
            satelliteCount.textContent = dataSystemStatus.gpsSatellites || 0;
        }

        // 更新经纬度显示
        const latEl = document.getElementById('gpsLatDisplay');
        const lngEl = document.getElementById('gpsLngDisplay');
        if (latEl && lngEl) {
            const lat = dataSystemStatus.gpsLatitude || 0;
            const lng = dataSystemStatus.gpsLongitude || 0;
            if (lat !== 0 || lng !== 0) {
                latEl.textContent = lat.toFixed(2) + '°N';
                lngEl.textContent = lng.toFixed(2) + '°E';
            } else {
                latEl.textContent = '--';
                lngEl.textContent = '--';
            }
        }

        // 更新卫星指示器激活状态
        if (satelliteIndicator) {
            const satellites = dataSystemStatus.gpsSatellites || 0;
            if (satellites >= 4) {
                satelliteIndicator.classList.add('active');
            } else {
                satelliteIndicator.classList.remove('active');
            }
        }

        // 计算 GPS 方位角（从起点到当前位置），用于 H 标记定位
        let gpsBearing = 0;
        let hasGpsBearing = false;

        if (dataSystemStatus.homeLatitude !== null && dataSystemStatus.homeLongitude !== null &&
            dataSystemStatus.gpsLatitude !== 0 && dataSystemStatus.gpsLongitude !== 0) {
            // 计算从起点到当前位置的方位角
            const lat1 = dataSystemStatus.homeLatitude * Math.PI / 180;
            const lat2 = dataSystemStatus.gpsLatitude * Math.PI / 180;
            const lon1 = dataSystemStatus.homeLongitude * Math.PI / 180;
            const lon2 = dataSystemStatus.gpsLongitude * Math.PI / 180;

            const dLon = lon2 - lon1;
            const x = Math.sin(dLon) * Math.cos(lat2);
            const y = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);

            gpsBearing = Math.atan2(x, y) * 180 / Math.PI;
            gpsBearing = (gpsBearing + 360) % 360; // 转换为 0-360 度
            hasGpsBearing = true;

            // 显示H标记
            radarHome.style.display = 'block';

            // 计算H在圆圈边缘的位置（相对于起点的相反方向）
            // 圆心(40,40), 外圈半径35, H标记放在内圈半径处确保不越界
            const homeAngle = (gpsBearing + 180) % 360;
            const homeRadius = 25; // H标记距离中心的距离（内圈）
            const homeRad = (homeAngle - 90) * Math.PI / 180; // 调整角度以匹配SVG坐标系
            const homeX = Math.cos(homeRad) * homeRadius;
            const homeY = Math.sin(homeRad) * homeRadius;

            radarHome.setAttribute('transform', `translate(40, 40) translate(${homeX}, ${homeY})`);
        } else {
            // 没有GPS数据或没有起始位置时，H隐藏
            radarHome.style.display = 'none';
        }

        // 箭头方向：优先使用 IMU yaw（实时航向），无 IMU 时回退到 GPS 方位角
        // 两者都不可用时隐藏箭头
        if (!dataSystemStatus.hasImuHeading && !hasGpsBearing) {
            radarArrow.style.display = 'none';
        } else {
            radarArrow.style.display = '';
            let arrowBearing = 0;
            if (dataSystemStatus.hasImuHeading) {
                // IMU attitude_yaw 为水平方向角（0=北, 顺时针增加）
                arrowBearing = dataSystemStatus.attitudeYaw;
            } else {
                // 回退：使用 GPS 计算的方位角
                arrowBearing = gpsBearing;
            }

            // 箭头默认朝上=北，rotate(0)=北, rotate(90)=东, rotate(180)=南, rotate(270)=西
            const svgAngle = arrowBearing;
            radarArrow.setAttribute('transform', `translate(40, 40) rotate(${svgAngle})`);
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

    // GPS 经纬度显示开关（默认隐藏）
    const gpsToggle = document.getElementById('gpsToggle');
    const gpsCoords = document.getElementById('gpsCoords');
    if (gpsToggle && gpsCoords) {
        gpsToggle.addEventListener('click', () => {
            const isVisible = gpsCoords.classList.toggle('visible');
            gpsToggle.classList.toggle('active', isVisible);
        });
    }

    // Initialize controllers
    initControllers();


    // 初始化速度表盘刻度
    initSpeedGaugeTicks();

    // 初始化速度表盘指针到0位置
    updateVehicleSpeedGauge(0);

    // 初始化油门比例仪表盘到0%
    updateThrottleGauge(0);

    // 初始化通道按键绑定器（全部16个通道的唯一数据源）
    if (typeof ChannelKeyBinder !== 'undefined') {
        window.channelKeyBinder = new ChannelKeyBinder();
        window.channelKeyBinder.loadFromConfig();
        window.channelKeyBinder.start();

        // 通道值变化时立即发送 SBUS 帧
        window.channelKeyBinder.setOnValueChange(() => {
            dataSendSbus(lastSentState.forward, lastSentState.turn);
        });

        console.log('ChannelKeyBinder started — single source of truth for all 16 channels');
    }

    // Send peer_close on page unload
    window.addEventListener('beforeunload', () => {
        // stopHeartbeat();
        stopControlLoop();
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
            // 连接失败时也启动重连
            dataStartWsReconnect(dataUrl);
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
                                // 收到 answer 后，重置重连状态
                                if (type === 'answer') {
                                    dataIsReconnecting = false;
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
        // 显式配置为可靠 + 有序（确保SCTP层自动重排）
        const dc = pc.createDataChannel('control', {
            ordered: true,  // 保证按序交付
            maxRetransmits: 10  // 最大重传次数（可靠传输）
        });
        // 保存 ws 引用到 data channel，用于错误处理时的重连
        dc._ws = ws;
        dataSetupDataChannel(dc, id);
        dataUpdateConnStatus('connecting', `CONNECTING TO ${id}`);

        // 添加超时检测：如果5秒内没有收到任何 ICE 候选或 answer，认为对端可能不在线
        pc._connectionTimeout = setTimeout(() => {
            const currentPc = dataPeerConnectionMap[id];
            if (currentPc === pc) {
                const state = pc.connectionState;
                const iceState = pc.iceConnectionState;

                // 如果还在 new 状态，说明对端没有响应，强制关闭并允许重连
                if (state === 'new') {
                    console.warn(`Data connection timeout for ${id}, state: ${state}/${iceState}, forcing close`);
                    updateStatus(`Data connection timeout to ${id} (no response)`);
                    try {
                        pc.close();
                    } catch (e) {
                        console.warn('Error closing timed-out data peer connection:', e);
                    }
                    if (dataPeerConnectionMap[id] === pc) {
                        delete dataPeerConnectionMap[id];
                    }
                    if (dataDataChannelMap[id]) {
                        delete dataDataChannelMap[id];
                    }
                    dataIsReconnecting = false; // 重置状态，允许重连
                }
            }
        }, 5000); // 5秒超时

        dataSendLocalDescription(ws, id, pc, 'offer');
    }

    function dataCreatePeerConnection(ws, id) {
        const pc = new RTCPeerConnection(rtcConfig);
        pc.oniceconnectionstatechange = () => {
            console.log(`DATA ICE Connection state: ${pc.iceConnectionState}`);
            // 更新 ICE 信息展示
            dataUpdateIceInfoDisplay(id);

            if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
                dataIsReconnecting = false; // ICE 连接成功，重置状态
                dataUpdateConnStatus('connected', 'CONNECTED');
                // 延迟再次更新 ICE 信息，确保获取到选中的候选对
                setTimeout(() => dataUpdateIceInfoDisplay(id), 1000);
                setTimeout(() => dataUpdateIceInfoDisplay(id), 3000);
            } else if (pc.iceConnectionState === 'failed') {
                console.error(`DATA ICE connection failed with ${id}, immediate reconnection`);
                dataIsReconnecting = false; // 重置连接状态，允许重连
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // ICE 连接失败时立即重连
                if (!dataReconnectInterval) {
                    dataStartAutoReconnect(ws, id);
                }
            } else if (pc.iceConnectionState === 'disconnected') {
                console.warn(`DATA ICE connection disconnected with ${id}`);
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // ICE disconnected 快速检查（1秒后）
                setTimeout(() => {
                    if (pc.iceConnectionState === 'disconnected' && !dataReconnectInterval) {
                        console.log(`DATA ICE still disconnected after 1s, reconnecting`);
                        dataIsReconnecting = false; // 重置连接状态，允许重连
                        dataStartAutoReconnect(ws, id);
                    }
                }, 1000);
            }
        };
        pc.onconnectionstatechange = () => {
            console.log(`DATA Connection state: ${pc.connectionState}`);
            // 更新 ICE 信息展示
            dataUpdateIceInfoDisplay(id);

            // 清理超时定时器
            if (pc._connectionTimeout) {
                clearTimeout(pc._connectionTimeout);
                pc._connectionTimeout = null;
            }

            if (pc.connectionState === 'connected') {
                dataIsReconnecting = false; // 重置连接状态
                dataUpdateConnStatus('connected', 'CONNECTED');
                dataToggleNoSignalOverlay(false);
                dataStopAutoReconnect(); // 连接成功，停止自动重连
                // 延迟再次更新 ICE 信息，确保获取到选中的候选对
                setTimeout(() => dataUpdateIceInfoDisplay(id), 1000);
                setTimeout(() => dataUpdateIceInfoDisplay(id), 3000);
            } else if (pc.connectionState === 'failed') {
                console.error(`DATA Connection failed with ${id}, immediate reconnection`);
                dataIsReconnecting = false; // 重置连接状态，允许重连
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // 立即重连，不延迟
                dataStartAutoReconnect(ws, id);
            } else if (pc.connectionState === 'disconnected') {
                console.warn(`DATA Connection disconnected with ${id}`);
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // disconnected 状态快速检查（1秒后）
                setTimeout(() => {
                    if (pc.connectionState === 'disconnected' && !dataReconnectInterval) {
                        console.log(`DATA Still disconnected after 1s, reconnecting`);
                        dataIsReconnecting = false; // 重置连接状态，允许重连
                        dataStartAutoReconnect(ws, id);
                    }
                }, 1000);
            } else if (pc.connectionState === 'closed') {
                console.log(`DATA Connection closed with ${id}`);
                dataIsReconnecting = false; // 重置连接状态，允许重连
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                dataToggleNoSignalOverlay(true);
                // Clean up the peer connection
                if (dataPeerConnectionMap[id] === pc) {
                    delete dataPeerConnectionMap[id];
                }
                if (dataDataChannelMap[id]) {
                    delete dataDataChannelMap[id];
                }
                // 连接关闭时立即重连
                if (!dataReconnectInterval) {
                    dataStartAutoReconnect(ws, id);
                }
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
            // 重连时复位状态并发送中位值，防止残留旧值
            lastSentState = { forward: 0, turn: 0 };
            dataSendSbus(0, 0);
            // 启动控制循环
            // startHeartbeat();
            startControlLoop();
            dataStopAutoReconnect(); // 数据通道打开，停止自动重连
        };
        dc.onclose = () => {
            updateStatus(`Data channel closed with ${id}`);
            if (dataCurrentDataChannel === dc) {
                dataCurrentDataChannel = null;
                dataUpdateConnStatus('disconnected', 'DISCONNECTED');
                // 停止控制循环
                // stopHeartbeat();
                stopControlLoop();
                // 启动自动重连
                if (dataSignalingWs) {
                    dataStartAutoReconnect(dataSignalingWs, id);
                }
            }
        };
        dc.onerror = (err) => {
            console.error(`DataChannel error with ${id}:`, err);
            updateStatus(`Data channel error with ${id}`);
            // 数据通道错误时可能需要重连
            if (!dataReconnectInterval) {
                // 延迟检查是否需要重连
                setTimeout(() => {
                    const pc = dataPeerConnectionMap[id];
                    if (!pc || pc.connectionState !== 'connected') {
                        if (dataSignalingWs) {
                            dataStartAutoReconnect(dataSignalingWs, id);
                        }
                    }
                }, 2000);
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
                    // 尝试解析为 JSON 系统状态（C++端通过 text DataChannel 发送）
                    try {
                        const statusData = JSON.parse(ev.data);
                        if (statusData && typeof statusData === 'object') {
                            console.log('Received JSON system status:', statusData);
                            dataHandleSystemStatusData(statusData);
                        }
                    } catch (e) {
                        console.log('Text message (non-JSON):', ev.data);
                    }
                }
            }
        };
        dataDataChannelMap[id] = dc;
        return dc;
    }

    // 自动重连功能：快速重连尝试
    function dataStartAutoReconnect(ws, id) {
        // 如果已经在重连，先停止
        dataStopAutoReconnect();

        updateStatus(`Auto-reconnecting to ${id}...`);
        dataUpdateConnStatus('connecting', 'RECONNECTING');
        console.log(`Starting data auto-reconnect to ${id}`);

        let reconnectAttempts = 0;
        let reconnectDelay = 500; // 初始重连延迟500ms（不要太快，避免ICE冲突）
        const maxReconnectDelay = 3000; // 最大重连延迟

        const tryReconnect = () => {
            reconnectAttempts++;

            // 检查 signaling 连接是否正常
            if (!ws || ws.readyState !== WebSocket.OPEN) {
                console.log('Data signaling connection lost, cannot reconnect');
                return;
            }

            // 检查是否已经有成功的连接
            const existingPc = dataPeerConnectionMap[id];
            if (existingPc) {
                const connectionState = existingPc.connectionState;
                const iceState = existingPc.iceConnectionState;

                // 只有真正连接成功了才停止重连
                if (connectionState === 'connected' && (iceState === 'connected' || iceState === 'completed')) {
                    console.log(`Data successfully connected to ${id}, stopping reconnection`);
                    dataStopAutoReconnect();
                    return;
                }

                // 如果正在连接中（checking 或 connecting），不要创建新连接
                if (connectionState === 'connecting' || connectionState === 'new' ||
                    iceState === 'checking' || iceState === 'new') {
                    console.log(`Data connection to ${id} in progress (${connectionState}/${iceState}), waiting...`);
                    dataIsReconnecting = true;

                    // 设置超时：如果 10s 后还在连接，强制重置
                    setTimeout(() => {
                        if (dataPeerConnectionMap[id] === existingPc) {
                            const currentState = existingPc.connectionState;
                            const currentIceState = existingPc.iceConnectionState;
                            if (currentState === 'connecting' || currentState === 'new' ||
                                currentIceState === 'checking' || currentIceState === 'new') {
                                console.log(`Data connection stuck for ${id}, forcing close`);
                                try {
                                    existingPc.close();
                                } catch (e) {
                                    console.warn('Error closing stuck data peer connection:', e);
                                }
                                delete dataPeerConnectionMap[id];
                                if (dataDataChannelMap[id]) {
                                    delete dataDataChannelMap[id];
                                }
                                dataIsReconnecting = false; // 重置状态，允许重连
                            }
                        }
                    }, 10000);

                    return;
                }

                // 如果是 failed 或 disconnected 状态，清理旧连接
                if (connectionState === 'failed' || connectionState === 'disconnected' ||
                    connectionState === 'closed') {
                    console.log(`Cleaning up old data connection in ${connectionState} state`);
                    try {
                        existingPc.close();
                    } catch (e) {
                        console.warn('Error closing old data peer connection:', e);
                    }
                    delete dataPeerConnectionMap[id];
                    if (dataDataChannelMap[id]) {
                        delete dataDataChannelMap[id];
                    }
                }
            }

            console.log(`Data reconnection attempt ${reconnectAttempts} to ${id} (delay: ${reconnectDelay}ms)`);

            // 标记正在连接
            dataIsReconnecting = true;

            // 创建新的 PeerConnection 并发送 offer
            try {
                dataOfferPeerConnection(ws, id);
            } catch (e) {
                console.error('Error during data reconnection:', e);
                dataIsReconnecting = false;
            }

            // 调整重连延迟（线性增加，避免指数退避太快）
            if (reconnectDelay < maxReconnectDelay) {
                reconnectDelay = Math.min(maxReconnectDelay, reconnectDelay + 100);
            }
        };

        // 立即尝试第一次连接
        tryReconnect();

        // 设置定时器继续重连
        dataReconnectInterval = setInterval(tryReconnect, reconnectDelay);
    }

    function dataStopAutoReconnect() {
        if (dataReconnectInterval) {
            clearInterval(dataReconnectInterval);
            dataReconnectInterval = null;
            console.log('Data auto-reconnect stopped');
        }
        // 重置连接状态
        dataIsReconnecting = false;
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
                    // 关键：WS 重连成功，必须停止之前基于旧 WS 的 PeerConnection 自动重连。
                    // 否则旧自动重连定时器会持续检查新 PC 状态，并在超时后
                    // 将新 PC 强制 kill，导致"连上瞬间又断开"的死循环。
                    dataStopAutoReconnect();
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

