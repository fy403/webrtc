    (function () {
        const ENDPOINT = 'ws://fy403.cn:8766'; // 作为 client 连接该端点

        // 协议常量，保持与 C++ 一致
        const kMagic0 = 0xAA;
        const kMagic1 = 0x55;
        const MSG_KEY = 0x01;
        const MSG_EMERGENCY_STOP = 0x02;
        const MSG_CYCLE_THROTTLE = 0x03;
        const MSG_STOP_ALL = 0x04;
        const MSG_QUIT = 0x05;
        const MSG_PING = 0x10;
        const KEY_W = 1;
        const KEY_S = 2;
        const KEY_A = 3;
        const KEY_D = 4;

        let ws = null;
        let reconnectTimer = null;
        let manualClose = false;

        const state = {
            W: false,
            A: false,
            S: false,
            D: false
        };

        const el = {
            status: document.getElementById('connStatus'),
            endpointText: document.getElementById('endpointText'),
            reconnectBtn: document.getElementById('reconnectBtn'),
            stopAllBtn: document.getElementById('stopAllBtn'),
            emgBtn: document.getElementById('emgBtn'),
            throttleBtn: document.getElementById('throttleBtn'),
            keyW: document.getElementById('keyW'),
            keyA: document.getElementById('keyA'),
            keyS: document.getElementById('keyS'),
            keyD: document.getElementById('keyD'),
            videoFrame: document.getElementById('videoFrame'),
            videoOverlay: document.getElementById('videoOverlay'),
            videoUrl: document.getElementById('videoUrl'),
            videoReloadBtn: document.getElementById('videoReloadBtn'),
            signalStrength: document.getElementById('signalStrength'),
            signalIndicator: document.getElementById('signalIndicator'),
            signalQuality: document.getElementById('signalQuality')
        };

        function setStatus(kind, text) {
            const color = kind === 'connected' ? 'dot-green' : kind === 'connecting' ? 'dot-yellow' : 'dot-red';
            el.status.innerHTML = '<span class="status-dot ' + color + '"></span><span class="mono">' + text + '</span>';
        }

        function checksum16(bytes) {
            let sum = 0;
            for (let i = 0; i < bytes.length; i++) sum = (sum + (bytes[i] & 0xFF)) & 0xFFFF;
            return sum & 0xFFFF;
        }

        function makeFrame(type, keyCode, value) {
            const buf = new Uint8Array(8);
            buf[0] = kMagic0;
            buf[1] = kMagic1;
            buf[2] = type & 0xFF;
            buf[3] = keyCode & 0xFF;
            buf[4] = value & 0xFF;
            buf[5] = 0;
            const cs = checksum16(buf.subarray(0, 6));
            buf[6] = (cs >> 8) & 0xFF;
            buf[7] = cs & 0xFF;
            return buf;
        }

        function sendFrame(type, keyCode, value) {
            if (!ws || ws.readyState !== WebSocket.OPEN) return;
            try {
                ws.send(makeFrame(type, keyCode, value));
            } catch (e) {}
        }

        function updateKeyVisual() {
            el.keyW.classList.toggle('active', state.W);
            el.keyA.classList.toggle('active', state.A);
            el.keyS.classList.toggle('active', state.S);
            el.keyD.classList.toggle('active', state.D);
        }

        function mapKeyCode(code) {
            switch (code) {
                case 'KeyW':
                    return KEY_W;
                case 'KeyS':
                    return KEY_S;
                case 'KeyA':
                    return KEY_A;
                case 'KeyD':
                    return KEY_D;
                default:
                    return 0;
            }
        }

        function onKeyDown(ev) {
            if (ev.repeat) return; // 忽略重复
            const kc = mapKeyCode(ev.code);
            if (kc) {
                const keyChar = ev.code.slice(3);
                if (!state[keyChar]) {
                    state[keyChar] = true;
                    updateKeyVisual();
                    sendFrame(MSG_KEY, kc, 1);
                }
            } else if (ev.code === 'KeyT') {
                sendFrame(MSG_EMERGENCY_STOP, 0, 0);
            } else if (ev.code === 'KeyF') {
                sendFrame(MSG_CYCLE_THROTTLE, 0, 0);
            } else if (ev.code === 'Space') {
                sendFrame(MSG_STOP_ALL, 0, 0);
            } else if (ev.code === 'KeyQ') {
                sendFrame(MSG_QUIT, 0, 0);
            }
        }

        function onKeyUp(ev) {
            const kc = mapKeyCode(ev.code);
            if (kc) {
                const keyChar = ev.code.slice(3);
                if (state[keyChar]) {
                    state[keyChar] = false;
                    updateKeyVisual();
                    sendFrame(MSG_KEY, kc, 0);
                }
            }
        }

        function attachKeyboard() {
            window.addEventListener('keydown', onKeyDown);
            window.addEventListener('keyup', onKeyUp);
        }

        function detachKeyboard() {
            window.removeEventListener('keydown', onKeyDown);
            window.removeEventListener('keyup', onKeyUp);
        }

        const MSG_SYSTEM_STATUS = 0x20;

        // 系统状态数据
        let systemStatus = {
            rxSpeed: 0,
            txSpeed: 0,
            cpuUsage: 0,
            ttyService: false,
            rtspService: false,
            signalStrength: -1, // 4G信号强度，默认-1表示未知
            sim_status: "UNKNOWN",
            lastUpdate: null
        };

        // 4G信号强度评估函数
        function evaluateSignalStrength(dbm) {
            // 根据4G信号强度标准评估
            if (dbm >= -70) return {
                level: 5,
                quality: "极好"
            }; // -50 to -70 dBm
            if (dbm >= -85) return {
                level: 4,
                quality: "良好"
            }; // -70 to -85 dBm
            if (dbm >= -100) return {
                level: 3,
                quality: "一般"
            }; // -85 to -100 dBm
            if (dbm >= -110) return {
                level: 2,
                quality: "较差"
            }; // -100 to -110 dBm
            return {
                level: 1,
                quality: "极差"
            }; // < -110 dBm
        }

        // 更新4G信号强度显示
        function updateSignalStrengthDisplay(dbm) {
            if (dbm === -1) {
                // 未知信号强度
                el.signalStrength.textContent = "-- dBm";
                el.signalQuality.textContent = "未知";

                // 重置信号指示器
                const bars = el.signalIndicator.querySelectorAll('.signal-bar');
                bars.forEach(bar => {
                    bar.classList.remove('active', 'moderate', 'weak');
                });
                return;
            }

            // 更新信号强度数值
            el.signalStrength.textContent = `${dbm} dBm`;

            // 评估信号质量
            const signalInfo = evaluateSignalStrength(dbm);
            el.signalQuality.textContent = signalInfo.quality;

            // 更新信号指示器
            const bars = el.signalIndicator.querySelectorAll('.signal-bar');
            bars.forEach((bar, index) => {
                // 清除所有样式
                bar.classList.remove('active', 'moderate', 'weak');

                // 根据信号级别设置样式
                if (index < signalInfo.level) {
                    if (signalInfo.level >= 4) {
                        bar.classList.add('active'); // 良好信号
                    } else if (signalInfo.level >= 3) {
                        bar.classList.add('moderate'); // 中等信号
                    } else {
                        bar.classList.add('weak'); // 弱信号
                    }
                }
            });
        }

        // 更新系统状态显示
        function updateSystemStatusDisplay() {
            const rxSpeedElement = document.getElementById('rxSpeed');
            const txSpeedElement = document.getElementById('txSpeed');
            const cpuUsageElement = document.getElementById('cpuUsage');
            const serviceStatusElement = document.getElementById('serviceStatus');
            const lastUpdateElement = document.getElementById('lastUpdate');
            const simStatusElement = document.getElementById('simStatus');

            if (rxSpeedElement) rxSpeedElement.textContent = systemStatus.rxSpeed.toFixed(2) + ' Kbit/s';
            if (txSpeedElement) txSpeedElement.textContent = systemStatus.txSpeed.toFixed(2) + ' Kbit/s';
            if (cpuUsageElement) cpuUsageElement.textContent = systemStatus.cpuUsage.toFixed(2) + '%';
            if (simStatusElement) simStatusElement.textContent = systemStatus.sim_status;

            const serviceStatus = [];
            if (systemStatus.ttyService) serviceStatus.push('✓ 控制信号');
            else serviceStatus.push('✗ 控制信号');

            if (systemStatus.rtspService) serviceStatus.push('✓ 视频信号');
            else serviceStatus.push('✗ 视频信号');

            if (serviceStatusElement) serviceStatusElement.textContent = serviceStatus.join(' | ');

            if (systemStatus.lastUpdate && lastUpdateElement) {
                const timeStr = systemStatus.lastUpdate.toLocaleTimeString();
                lastUpdateElement.textContent = '最后更新: ' + timeStr;
            }

            // 更新4G信号强度显示
            updateSignalStrengthDisplay(systemStatus.signalStrength);
        }

        // 从十六进制字符串解析二进制数据
        function parseHexString(hexString) {
            // 移除可能的前缀和空格
            hexString = hexString.replace(/Binary data:\s*/gi, '').replace(/\s/g, '');

            // 检查字符串长度是否为偶数
            if (hexString.length % 2 !== 0) {
                console.error('十六进制字符串长度不正确');
                return null;
            }

            const bytes = new Uint8Array(hexString.length / 2);
            for (let i = 0; i < hexString.length; i += 2) {
                bytes[i / 2] = parseInt(hexString.substr(i, 2), 16);
            }
            return bytes;
        }

        // 解析系统状态帧
        function parseSystemStatusFrame(data) {
            try {
                // console.log('解析数据，长度:', data.length);

                // 检查帧头
                if (data[0] !== 0xAA || data[1] !== 0x55) {
                    console.error('无效的帧头:', data[0].toString(16), data[1].toString(16));
                    return null;
                }

                // 检查消息类型
                if (data[2] !== MSG_SYSTEM_STATUS) {
                    console.error('不是系统状态消息:', data[2].toString(16));
                    return null;
                }

                // 获取数据长度
                const dataLength = (data[3] << 8) | data[4];
                // console.log('数据长度:', dataLength);

                // 检查数据长度是否合理
                if (dataLength > data.length - 7) {
                    console.error('数据长度超出范围:', dataLength);
                    return null;
                }

                // 提取数据部分
                let dataStr = '';
                for (let i = 5; i < 5 + dataLength; i++) {
                    dataStr += String.fromCharCode(data[i]);
                }
                // console.log('数据字符串:', dataStr);

                // 解析键值对
                const lines = dataStr.split('\r\n');
                const statusData = {};

                for (const line of lines) {
                    if (line.trim()) {
                        const separatorIndex = line.indexOf(':');
                        if (separatorIndex !== -1) {
                            const key = line.substring(0, separatorIndex).trim();
                            const value = line.substring(separatorIndex + 1).trim();
                            if (key && value !== undefined) {
                                statusData[key] = value;
                            }
                        }
                    }
                }

                return statusData;
            } catch (error) {
                console.error('解析系统状态帧时出错:', error);
                return null;
            }
        }

        // 处理系统状态数据
        function handleSystemStatusData(statusData) {
            if (!statusData) return;

            // console.log('处理状态数据:', statusData);

            // 更新系统状态对象
            if (statusData.rx_speed !== undefined) {
                systemStatus.rxSpeed = parseInt(statusData.rx_speed) * 8 / 100;
            }
            if (statusData.tx_speed !== undefined) {
                systemStatus.txSpeed = parseInt(statusData.tx_speed) * 8 / 100;
            }
            if (statusData.cpu_usage !== undefined) {
                systemStatus.cpuUsage = parseInt(statusData.cpu_usage) / 100;
            }
            if (statusData.tty_service !== undefined) {
                systemStatus.ttyService = statusData.tty_service === '1';
            }
            if (statusData.rtsp_service !== undefined) {
                systemStatus.rtspService = statusData.rtsp_service === '1';
            }
            if (statusData['4g_signal'] !== undefined) {
                // 解析4G信号强度，格式为"21,0"
                const signalParts = statusData['4g_signal'].split(',');
                if (signalParts.length >= 1) {
                    systemStatus.signalStrength = parseInt(signalParts[0]);
                }
            }

            if (statusData.sim_status !== undefined) {
                systemStatus.sim_status = statusData.sim_status;
            }

            systemStatus.lastUpdate = new Date();

            // 更新显示
            updateSystemStatusDisplay();

            // 更新连接状态
            setStatus('connected', '已连接');
        }

        function connect() {
            clearTimeout(reconnectTimer);
            setStatus('connecting', 'CONNECTING');
            el.endpointText.textContent = ENDPOINT;
            manualClose = false;
            try {
                ws = new WebSocket(ENDPOINT);
                ws.binaryType = 'arraybuffer';
                ws.onopen = function () {
                    setStatus('connected', 'CONNECTED');
                };
                ws.onmessage = function (ev) {
                    // console.log('收到原始数据，类型:', typeof ev.data);
                    if (ev.data instanceof ArrayBuffer) {
                        // 处理二进制数据
                        const data = new Uint8Array(ev.data);
                        console.log('二进制数据长度:', data.length);
                        // console.log('二进制数据:', Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' '));

                        // 解析系统状态帧
                        const statusData = parseSystemStatusFrame(data);
                        if (statusData) {
                            // console.log('成功解析状态数据');
                            handleSystemStatusData(statusData);
                        } else {
                            console.log('解析状态数据失败');
                        }
                    } else if (typeof ev.data === 'string') {
                        // 处理文本数据
                        // console.log('文本数据:', ev.data);
                        if (ev.data.startsWith('Binary data:')) {
                            // 这是十六进制字符串格式的二进制数据
                            const binaryData = parseHexString(ev.data);
                            if (binaryData) {
                                // console.log('转换后的二进制数据:', Array.from(binaryData).map(b => b.toString(16).padStart(2, '0')).join(' '));

                                // 解析系统状态帧
                                const statusData = parseSystemStatusFrame(binaryData);
                                if (statusData) {
                                    // console.log('成功解析状态数据');
                                    handleSystemStatusData(statusData);
                                } else {
                                    console.log('解析状态数据失败');
                                }
                            }
                        } else {
                            // 其他文本消息
                            console.log('不支持的：普通文本消息:', ev.data);
                        }
                    } else {
                        console.log('未知数据类型:', typeof ev.data);
                    }
                };
                ws.onerror = function () {
                    /* 显示在 close 中处理 */
                };
                ws.onclose = function () {
                    setStatus('disconnected', 'DISCONNECTED');
                    if (!manualClose) {
                        reconnectTimer = setTimeout(connect, 1500);
                    }
                };
            } catch (e) {
                setStatus('disconnected', 'DISCONNECTED');
                reconnectTimer = setTimeout(connect, 1500);
            }
        }
        // 心跳发送
        setInterval(function () {
            sendFrame(MSG_PING, 0, 0);
        }, 1000);

        el.reconnectBtn.addEventListener('click', function () {
            if (ws) {
                manualClose = true;
                try {
                    ws.close();
                } catch (e) {}
            }
            connect();
        });
        el.stopAllBtn.addEventListener('click', function () {
            sendFrame(MSG_STOP_ALL, 0, 0);
        });
        el.emgBtn.addEventListener('click', function () {
            sendFrame(MSG_EMERGENCY_STOP, 0, 0);
        });
        el.throttleBtn.addEventListener('click', function () {
            sendFrame(MSG_CYCLE_THROTTLE, 0, 0);
        });
        attachKeyboard();
        connect();
    })();