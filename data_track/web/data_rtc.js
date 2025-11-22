
window.addEventListener('load', () => {

  // 协议常量，保持与 C++ 一致
  const kMagic0 = 0xAA;
  const kMagic1 = 0x55;
  const MSG_KEY = 0x01;
  const MSG_EMERGENCY_STOP = 0x02;
  const MSG_CYCLE_THROTTLE = 0x03;
  const MSG_STOP_ALL = 0x04;
  const MSG_QUIT = 0x05;
  const MSG_PING = 0x10;
  const MSG_SYSTEM_STATUS = 0x20;
  const KEY_W = 1;
  const KEY_S = 2;
  const KEY_A = 3;
  const KEY_D = 4;

  const dataConfig = {
    iceServers: [{
      urls: 'stun:stun.l.google.com:19302', // change to your STUN server
    }],
  };

  const dataLocalId = dataRandomId(5);

  const dataUrl = `ws://fy403.cn:8000/${dataLocalId}`;

  const dataPeerConnectionMap = {};
  const dataDataChannelMap = {};

  let dataCurrentDataChannel = null;

  const dataOfferId = document.getElementById('dataOfferId');
  const dataOfferBtn = document.getElementById('dataOfferBtn');
  const dataLocalIdElement = document.getElementById('dataLocalId');
  const dataStatusDiv = document.getElementById('dataStatus');
  dataLocalIdElement.textContent = dataLocalId;

  // 控制状态
  const dataState = {
    W: false,
    A: false,
    S: false,
    D: false
  };

  // 获取控制元素
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
    forwardBtn: document.getElementById('forwardBtn'),
    backwardBtn: document.getElementById('backwardBtn'),
    leftBtn: document.getElementById('leftBtn'),
    rightBtn: document.getElementById('rightBtn'),
    connStatus: document.getElementById('connStatus'),
    signalStrength: document.getElementById('signalStrength'),
    signalIndicator: document.getElementById('signalIndicator'),
    signalQuality: document.getElementById('signalQuality'),
    serviceStatus: document.getElementById('serviceStatus'),
    simStatus: document.getElementById('simStatus'),
    rxSpeed: document.getElementById('rxSpeed'),
    txSpeed: document.getElementById('txSpeed'),
    cpuUsage: document.getElementById('cpuUsage'),
    lastUpdate: document.getElementById('lastUpdate')
  };

  // 系统状态数据
  let dataSystemStatus = {
    rxSpeed: 0,
    txSpeed: 0,
    cpuUsage: 0,
    ttyService: false,
    rtspService: false,
    signalStrength: -1, // 4G信号强度，默认-1表示未知
    sim_status: "UNKNOWN",
    lastUpdate: null
  };

  function updateStatus(message) {
    dataStatusDiv.textContent = 'Status: ' + message;
    console.log('Status: ' + message);
  }

  console.log('Connecting to signaling...');
  dataOpenSignaling(dataUrl)
    .then((ws) => {
      console.log('WebSocket connected, signaling ready');
      updateStatus('Signaling connected');
      dataOfferId.disabled = false;
      dataOfferBtn.disabled = false;
      dataOfferBtn.onclick = () => dataOfferPeerConnection(ws, dataOfferId.value);
      // 如果有默认的offerId值，则自动连接
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

  // Helper function to show/hide the "No Signal" overlay
  function dataToggleNoSignalOverlay(show) {
    const overlay = document.getElementById('noSignalOverlay');
    if (overlay) {
      overlay.style.display = show ? 'flex' : 'none';
    }
  }

  // 更新连接状态显示
  function dataUpdateConnStatus(kind, text) {
    if (!dataElements.connStatus) return;
    const color = kind === 'connected' ? 'dot-green' : kind === 'connecting' ? 'dot-yellow' : 'dot-red';
    dataElements.connStatus.innerHTML = '<span class="status-dot ' + color + '"></span><span class="mono">' + text + '</span>';
  }

  // 更新按键视觉反馈
  function dataUpdateKeyVisual() {
    if (dataElements.keyW) dataElements.keyW.classList.toggle('active', dataState.W);
    if (dataElements.keyA) dataElements.keyA.classList.toggle('active', dataState.A);
    if (dataElements.keyS) dataElements.keyS.classList.toggle('active', dataState.S);
    if (dataElements.keyD) dataElements.keyD.classList.toggle('active', dataState.D);
  }

  // 映射键盘码
  function dataMapKeyCode(code) {
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

  // 计算校验和
  function dataChecksum16(bytes) {
    let sum = 0;
    for (let i = 0; i < bytes.length; i++) sum = (sum + (bytes[i] & 0xFF)) & 0xFFFF;
    return sum & 0xFFFF;
  }

  // 创建控制帧
  function dataMakeFrame(type, keyCode, value) {
    const buf = new Uint8Array(8);
    buf[0] = kMagic0;
    buf[1] = kMagic1;
    buf[2] = type & 0xFF;
    buf[3] = keyCode & 0xFF;
    buf[4] = value & 0xFF;
    buf[5] = 0;
    const cs = dataChecksum16(buf.subarray(0, 6));
    buf[6] = (cs >> 8) & 0xFF;
    buf[7] = cs & 0xFF;
    return buf;
  }

  // 发送控制帧
  function dataSendFrame(type, keyCode, value) {
    if (!dataCurrentDataChannel || dataCurrentDataChannel.readyState !== 'open') {
      console.warn('DataChannel is not open, cannot send frame');
      return;
    }
    try {
      const frame = dataMakeFrame(type, keyCode, value);
      dataCurrentDataChannel.send(frame);
    } catch (e) {
      console.error('Error sending frame:', e);
    }
  }

  // 键盘事件处理
  function dataOnKeyDown(ev) {
    if (ev.repeat) return; // 忽略重复按键
    const kc = dataMapKeyCode(ev.code);
    if (kc) {
      const keyChar = ev.code.slice(3);
      if (!dataState[keyChar]) {
        dataState[keyChar] = true;
        dataUpdateKeyVisual();
        console.log('!!!Key down:', keyChar, kc, 1);
        dataSendFrame(MSG_KEY, kc, 1);
      }
    } else if (ev.code === 'KeyT') {
      dataSendFrame(MSG_EMERGENCY_STOP, 0, 0);
    } else if (ev.code === 'KeyF') {
      dataSendFrame(MSG_CYCLE_THROTTLE, 0, 0);
    } else if (ev.code === 'Space') {
      dataSendFrame(MSG_STOP_ALL, 0, 0);
    } else if (ev.code === 'KeyQ') {
      dataSendFrame(MSG_QUIT, 0, 0);
    }
  }

  function dataOnKeyUp(ev) {
    const kc = dataMapKeyCode(ev.code);
    if (kc) {
      const keyChar = ev.code.slice(3);
      if (dataState[keyChar]) {
        dataState[keyChar] = false;
        dataUpdateKeyVisual();
        dataSendFrame(MSG_KEY, kc, 0);
      }
    }
  }

  // 附加键盘事件监听器
  function dataAttachKeyboard() {
    window.addEventListener('keydown', dataOnKeyDown);
    window.addEventListener('keyup', dataOnKeyUp);

  }

  // 移除键盘事件监听器
  function dataDetachKeyboard() {
    window.removeEventListener('keydown', dataOnKeyDown);
    window.removeEventListener('keyup', dataOnKeyUp);

  }

  // 4G信号强度评估函数
  function dataEvaluateSignalStrength(dbm) {
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
  function dataUpdateSignalStrengthDisplay(dbm) {
    if (dbm === -1 || !dataElements.signalStrength || !dataElements.signalQuality || !dataElements.signalIndicator) {
      // 未知信号强度
      if (dataElements.signalStrength) dataElements.signalStrength.textContent = "-- dBm";
      if (dataElements.signalQuality) dataElements.signalQuality.textContent = "未知";

      // 重置信号指示器
      if (dataElements.signalIndicator) {
        const bars = dataElements.signalIndicator.querySelectorAll('.signal-bar');
        bars.forEach(bar => {
          bar.classList.remove('active', 'moderate', 'weak');
        });
      }
      return;
    }

    // 更新信号强度数值
    dataElements.signalStrength.textContent = `${dbm} dBm`;

    // 评估信号质量
    const signalInfo = dataEvaluateSignalStrength(dbm);
    dataElements.signalQuality.textContent = signalInfo.quality;

    // 更新信号指示器
    const bars = dataElements.signalIndicator.querySelectorAll('.signal-bar');
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
  function dataUpdateSystemStatusDisplay() {
    if (dataElements.rxSpeed) dataElements.rxSpeed.textContent = dataSystemStatus.rxSpeed.toFixed(2) + ' Kbit/s';
    if (dataElements.txSpeed) dataElements.txSpeed.textContent = dataSystemStatus.txSpeed.toFixed(2) + ' Kbit/s';
    if (dataElements.cpuUsage) dataElements.cpuUsage.textContent = dataSystemStatus.cpuUsage.toFixed(2) + '%';
    if (dataElements.simStatus) dataElements.simStatus.textContent = dataSystemStatus.sim_status;

    if (dataElements.serviceStatus) {
      const serviceStatus = [];
      if (dataSystemStatus.ttyService) serviceStatus.push('✓ 控制');
      else serviceStatus.push('✗ 控制');

      if (dataSystemStatus.rtspService) serviceStatus.push('✓ 视频');
      else serviceStatus.push('✗ 视频');

      dataElements.serviceStatus.textContent = serviceStatus.join(' | ');
    }

    if (dataSystemStatus.lastUpdate && dataElements.lastUpdate) {
      const timeStr = dataSystemStatus.lastUpdate.toLocaleTimeString();
      dataElements.lastUpdate.textContent = timeStr;
    }

    // 更新4G信号强度显示
    dataUpdateSignalStrengthDisplay(dataSystemStatus.signalStrength);
  }

  // 从十六进制字符串解析二进制数据
  function dataParseHexString(hexString) {
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
  function dataParseSystemStatusFrame(data) {
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
  function dataHandleSystemStatusData(statusData) {
    if (!statusData) return;

    // console.log('处理状态数据:', statusData);

    // 更新系统状态对象
    if (statusData.rx_speed !== undefined) {
      dataSystemStatus.rxSpeed = parseInt(statusData.rx_speed) * 8 / 100;
    }
    if (statusData.tx_speed !== undefined) {
      dataSystemStatus.txSpeed = parseInt(statusData.tx_speed) * 8 / 100;
    }
    if (statusData.cpu_usage !== undefined) {
      dataSystemStatus.cpuUsage = parseInt(statusData.cpu_usage) / 100;
    }
    if (statusData.tty_service !== undefined) {
      dataSystemStatus.ttyService = statusData.tty_service === '1';
    }
    if (statusData.rtsp_service !== undefined) {
      dataSystemStatus.rtspService = statusData.rtsp_service === '1';
    }
    if (statusData['4g_signal'] !== undefined) {
      // 解析4G信号强度，格式为"21,0"
      const signalParts = statusData['4g_signal'].split(',');
      if (signalParts.length >= 1) {
        dataSystemStatus.signalStrength = parseInt(signalParts[0]);
      }
    }

    if (statusData.sim_status !== undefined) {
      dataSystemStatus.sim_status = statusData.sim_status;
    }

    dataSystemStatus.lastUpdate = new Date();

    // 更新显示
    dataUpdateSystemStatusDisplay();

    // 更新连接状态
    dataUpdateConnStatus('connected', 'CONNECTED');
  }

  // 心跳发送
  setInterval(function () {
    dataSendFrame(MSG_PING, 0, 0);
  }, 1000);

  // 绑定控制按钮事件
  if (dataElements.reconnectBtn) {
    dataElements.reconnectBtn.addEventListener('click', function () {
      // 重新连接逻辑
      window.location.reload();
    });
  }

  if (dataElements.stopAllBtn) {
    dataElements.stopAllBtn.addEventListener('click', function () {
      dataSendFrame(MSG_STOP_ALL, 0, 0);
    });
  }

  if (dataElements.emgBtn) {
    dataElements.emgBtn.addEventListener('click', function () {
      dataSendFrame(MSG_EMERGENCY_STOP, 0, 0);
    });
  }

  if (dataElements.throttleBtn) {
    console.log('绑定油门按钮事件');
    dataElements.throttleBtn.addEventListener('click', function () {
      dataSendFrame(MSG_CYCLE_THROTTLE, 0, 0);
    });
  }

  if (dataElements.reconnectBtnMobile) {
    console.log('绑定重新连接按钮事件');
    dataElements.reconnectBtnMobile.addEventListener('click', function () {
      // 重新连接逻辑
      window.location.reload();
    });
  }

  if (dataElements.stopAllBtnMobile) {
    dataElements.stopAllBtnMobile.addEventListener('click', function () {
    console.log('绑定停止所有按钮事件');
      dataSendFrame(MSG_STOP_ALL, 0, 0);
    });
  }

  if (dataElements.emgBtnMobile) {
    dataElements.emgBtnMobile.addEventListener('click', function () {
    console.log('绑定紧急停止按钮事件');
      dataSendFrame(MSG_EMERGENCY_STOP, 0, 0);
    });
  }

  if (dataElements.throttleBtnMobile) {
    dataElements.throttleBtnMobile.addEventListener('click', function () {
      console.log('油门按钮点击');
      dataSendFrame(MSG_CYCLE_THROTTLE, 0, 0);
    });
  }

  // Unified handler for button press/release events
  function handleDirectionButton(keyCode, pressed) {
    if (pressed) {
      console.log(`${keyCode === 1 ? '前进' : keyCode === 2 ? '后退' : keyCode === 3 ? '左转' : '右转'}按钮按下`);
      dataSendFrame(MSG_KEY, keyCode, 1);
    } else {
      console.log(`${keyCode === 1 ? '前进' : keyCode === 2 ? '后退' : keyCode === 3 ? '左转' : '右转'}按钮释放`);
      dataSendFrame(MSG_KEY, keyCode, 0);
    }
  }

  if (dataElements.forwardBtn){
    dataElements.forwardBtn.addEventListener('touchstart', function () {
      handleDirectionButton(1, true);
    });
    dataElements.forwardBtn.addEventListener('touchend', function () {
      handleDirectionButton(1, false);
    });
  }

  if (dataElements.backwardBtn) {
    dataElements.backwardBtn.addEventListener('touchstart', function () {
      handleDirectionButton(2, true);
    });
    dataElements.backwardBtn.addEventListener('touchend', function () {
      handleDirectionButton(2, false);
    });
  }

  if (dataElements.leftBtn) {
    dataElements.leftBtn.addEventListener('touchstart', function () {
      handleDirectionButton(3, true);
    });
    dataElements.leftBtn.addEventListener('touchend', function () {
      handleDirectionButton(3, false);
    });
  }

  if (dataElements.rightBtn) {
    dataElements.rightBtn.addEventListener('touchstart', function (e) {
      handleDirectionButton(4, true);
    });
    dataElements.rightBtn.addEventListener('touchend', function (e) {
      handleDirectionButton(4, false);
    });
  }

  // 附加键盘监听
  dataAttachKeyboard();

  function dataOpenSignaling(url) {
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
        console.log(message);
        const {
          id,
          type
        } = message;

        let pc = dataPeerConnectionMap[id];
        if (!pc) {
          if (type != 'offer')
            return;

          // Create PeerConnection for answer
          console.log(`Answering to ${id}`);
          updateStatus(`Incoming call from ${id}`);
          pc = dataCreatePeerConnection(ws, id);
        }

        switch (type) {
          case 'offer':
          case 'answer':
            pc.setRemoteDescription({
              sdp: message.description,
              type: message.type,
            }).then(() => {
              if (type == 'offer') {
                // Send answer
                updateStatus(`Creating answer for ${id}`);
                dataSendLocalDescription(ws, id, pc, 'answer');
              }
            }).catch((error) => {
              updateStatus(`Error setting remote ${type}: ${err.message}`);
              console.error(`Error setting remote ${type}:`, err);
            });
            break;

          case 'candidate':
            pc.addIceCandidate({
              candidate: message.candidate,
              sdpMid: message.mid,
            });
            break;
        }
      }
    });
  }

  function dataOfferPeerConnection(ws, id) {
    if (!id) {
      alert('Please enter a remote ID');
      return;
    }

    // Create PeerConnection
    console.log(`Offering to ${id}`);
    updateStatus(`Offering to ${id}`);
    dataUpdateConnStatus('connecting', `CONNECTING TO ${id}`);
    const pc = dataCreatePeerConnection(ws, id);

    // Create DataChannel
    const label = "control";
    console.log(`Creating DataChannel with label "${label}"`);
    const dc = pc.createDataChannel(label);
    dataSetupDataChannel(dc, id);

    // Send offer
    updateStatus(`Creating offer for ${id}`);
    dataSendLocalDescription(ws, id, pc, 'offer');
  }

  // Create and setup a PeerConnection
  function dataCreatePeerConnection(ws, id) {
    const pc = new RTCPeerConnection(dataConfig);
    pc.oniceconnectionstatechange = () => {
      console.log(`Connection state: ${pc.iceConnectionState}`);
      updateStatus(`ICE: ${pc.iceConnectionState}`);
      if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
        updateStatus(`Connected to ${id}`);
        dataUpdateConnStatus('connected', 'CONNECTED');
      } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
        updateStatus(`Connection failed with ${id}`);
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
        dataToggleNoSignalOverlay(true);
      }
    };

    pc.onconnectionstatechange = () => {
      console.log(`Connection state: ${pc.connectionState}`);
      updateStatus(`Connection: ${pc.connectionState}`);

      if (pc.connectionState === 'connected') {
        dataUpdateConnStatus('connected', 'CONNECTED');
        updateStatus(`Connected to ${id}`);
      } else if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected' || pc.connectionState === 'closed') {
        updateStatus(`Connection failed with ${id}`);
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
        dataToggleNoSignalOverlay(true);
      }
    };

    pc.onicecandidate = (e) => {
      if (e.candidate && e.candidate.candidate) {
        // Send candidate
        dataSendLocalCandidate(ws, id, e.candidate);
      }
    };

    pc.ondatachannel = (e) => {
      const dc = e.channel;
      console.log(`DataChannel from ${id} received with label "${dc.label}"`);
      dataSetupDataChannel(dc, id);
    };

    pc.ontrack = (e) => {
      console.log('Received remote track:', e.track.kind, e.track.id, e.track.readyState);
    };

    dataPeerConnectionMap[id] = pc;
    return pc;
  }

  // Setup a DataChannel
  function dataSetupDataChannel(dc, id) {
    dc.onopen = () => {
      console.log(`DataChannel from ${id} open`);
      updateStatus(`Data channel open with ${id}`);
      dataCurrentDataChannel = dc;
      dataUpdateConnStatus('connected', 'CONNECTED');

      // 发送初始消息
      dc.send(`Hello from ${dataLocalId}`);
    };

    dc.onclose = () => {
      console.log(`DataChannel from ${id} closed`);
      updateStatus(`Data channel closed with ${id}`);
      if (dataCurrentDataChannel === dc) {
        dataCurrentDataChannel = null;
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
      }
    };

    dc.onmessage = (ev) => {
      // console.log(`Message from ${id} received:`, ev.data);

      if (ev.data instanceof ArrayBuffer) {
        // 处理二进制数据
        const data = new Uint8Array(ev.data);
        // console.log('二进制数据长度:', data.length);

        // 解析系统状态帧
        const statusData = dataParseSystemStatusFrame(data);
        if (statusData) {
          dataHandleSystemStatusData(statusData);
        } else {
          console.log('解析状态数据失败');
        }
      } else if (typeof ev.data === 'string') {
        // 处理文本数据
        console.log('文本数据:', ev.data);
        if (ev.data.startsWith('Binary data:')) {
          // 这是十六进制字符串格式的二进制数据
          const binaryData = dataParseHexString(ev.data);
          if (binaryData) {
            // 解析系统状态帧
            const statusData = dataParseSystemStatusFrame(binaryData);
            if (statusData) {
              dataHandleSystemStatusData(statusData);
            } else {
              console.log('解析状态数据失败');
            }
          }
        } else {
          // 其他文本消息
          console.log('普通文本消息:', ev.data);
        }
      } else {
        console.log('未知数据类型:', typeof ev.data);
      }
    };

    dataDataChannelMap[id] = dc;
    return dc;
  }

  function dataSendLocalDescription(ws, id, pc, type) {
    const options = type === 'offer' ? {
      offerToReceiveAudio: true,
      offerToReceiveVideo: true
    } : {};

    (type == 'offer' ? pc.createOffer(options) : pc.createAnswer())
    .then((desc) => pc.setLocalDescription(desc))
      .then(() => {
        const {
          sdp,
          type
        } = pc.localDescription;
        ws.send(JSON.stringify({
          id,
          type,
          description: sdp,
        }));
      }).catch(err => {
        console.error(`Error creating ${type}:`, err);
        updateStatus(`Error creating ${type}: ${err.message}`);
      });
  }

  function dataSendLocalCandidate(ws, id, cand) {
    const {
      candidate,
      sdpMid
    } = cand;
    ws.send(JSON.stringify({
      id,
      type: 'candidate',
      candidate,
      mid: sdpMid,
    }));
  }

  // Helper function to generate a random ID
  function dataRandomId(length) {
    const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
    return [...Array(length)].map(pickRandom).join('');
  }

});