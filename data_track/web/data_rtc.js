window.addEventListener('load', () => {
  // Only keep status frame id from legacy protocol; control uses SBUS.
  const MSG_SYSTEM_STATUS = 0x20;

  const dataLocalId = dataRandomId(5);
  const dataUrl = `ws://fy403.cn:8000/${dataLocalId}`;
  const dataConfig = {
    iceServers: [
      { urls: ['stun:stun.l.google.com:19302'] },
      { urls: ['turn:tx.fy403.cn:3478?transport=udp'], username: 'fy403', credential: 'qwertyuiop' },
    ],
  };


  const dataPeerConnectionMap = {};
  const dataDataChannelMap = {};
  let dataCurrentDataChannel = null;

  const dataOfferId = document.getElementById('dataOfferId');
  const dataOfferBtn = document.getElementById('dataOfferBtn');
  const dataLocalIdElement = document.getElementById('dataLocalId');
  const dataStatusDiv = document.getElementById('dataStatus');
  dataLocalIdElement.textContent = dataLocalId;

  // UI state
  const dataState = { W: false, A: false, S: false, D: false };

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

  // SBUS control pipeline
  const controllerManager = new ControllerManager((state) => {
    dataSendSbus(state.forward || 0, state.turn || 0);
  });

  function dataSendSbus(forward, turn) {
    if (!dataCurrentDataChannel || dataCurrentDataChannel.readyState !== 'open') return;
    uiSpeed = Math.round(Math.abs(forward) * 100);
    dataUpdateSystemStatusDisplay();
    try {
      const frame = SBUSEncoder.encode({ ch1: forward, ch2: turn });
      dataCurrentDataChannel.send(frame);
    } catch (e) {
      console.error('Failed to send SBUS frame', e);
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

    const joystick = new VirtualJoystickController({
      elements: {
        container: dataElements.joystickContainer,
        base: dataElements.joystickBase,
        handle: dataElements.joystickHandle,
      },
    });

    const xbox = new XboxController({});

    controllerManager.register('keyboard', keyboard, 10);
    controllerManager.register('xbox', xbox, 8);
    controllerManager.register('joystick', joystick, 5);
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

  function dataUpdateSystemStatusDisplay() {
    const { rxSpeed, txSpeed, cpuUsage, ttyService, rtspService, signalStrength, sim_status, lastUpdate } =
      dataSystemStatus;

    if (dataElements.rxSpeed) dataElements.rxSpeed.textContent = `${rxSpeed.toFixed(2)} KB/s`;
    if (dataElements.txSpeed) dataElements.txSpeed.textContent = `${txSpeed.toFixed(2)} KB/s`;
    if (dataElements.cpuUsage) dataElements.cpuUsage.textContent = `${cpuUsage.toFixed(2)}%`;
    if (dataElements.serviceStatus)
      dataElements.serviceStatus.textContent = `TTY:${ttyService ? 'ON' : 'OFF'} / RTSP:${rtspService ? 'ON' : 'OFF'}`;
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
      if (data[0] !== 0xAA || data[1] !== 0x55) return null;
      if (data[2] !== MSG_SYSTEM_STATUS) return null;

      const dataLength = (data[3] << 8) | data[4];
      if (dataLength > data.length - 7) return null;

      let dataStr = '';
      for (let i = 5; i < 5 + dataLength; i++) {
        dataStr += String.fromCharCode(data[i]);
      }

      const lines = dataStr.split('\r\n');
      const statusData = {};
      for (const line of lines) {
        if (!line.trim()) continue;
        const separatorIndex = line.indexOf(':');
        if (separatorIndex !== -1) {
          const key = line.substring(0, separatorIndex).trim();
          const value = line.substring(separatorIndex + 1).trim();
          if (key && value !== undefined) statusData[key] = value;
        }
      }
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

  // Initialize controllers
  initControllers();

  // Connect signaling
  console.log('Connecting to signaling...');
  dataOpenSignaling(dataUrl)
    .then((ws) => {
      updateStatus('Signaling connected');
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
        if (typeof e.data !== 'string') return;
        const message = JSON.parse(e.data);
        const { id, type } = message;
        let pc = dataPeerConnectionMap[id];
        if (!pc) {
          if (type !== 'offer') return;
          pc = dataCreatePeerConnection(ws, id);
        }
        switch (type) {
          case 'offer':
          case 'answer':
            pc.setRemoteDescription({ sdp: message.description, type: message.type })
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
            pc.addIceCandidate({ candidate: message.candidate, sdpMid: message.mid });
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
    const pc = new RTCPeerConnection(dataConfig);
    pc.oniceconnectionstatechange = () => {
      if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
        dataUpdateConnStatus('connected', 'CONNECTED');
      } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
        dataToggleNoSignalOverlay(true);
      }
    };
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'connected') {
        dataUpdateConnStatus('connected', 'CONNECTED');
        dataToggleNoSignalOverlay(false);
      } else if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
        dataToggleNoSignalOverlay(true);
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
    };
    dc.onclose = () => {
      updateStatus(`Data channel closed with ${id}`);
      if (dataCurrentDataChannel === dc) {
        dataCurrentDataChannel = null;
        dataUpdateConnStatus('disconnected', 'DISCONNECTED');
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

  function dataSendLocalDescription(ws, id, pc, type) {
    const options = type === 'offer' ? { offerToReceiveAudio: true, offerToReceiveVideo: true } : {};
    (type === 'offer' ? pc.createOffer(options) : pc.createAnswer())
      .then((desc) => pc.setLocalDescription(desc))
      .then(() => {
        const { sdp, type } = pc.localDescription;
        ws.send(JSON.stringify({ id, type, description: sdp }));
      })
      .catch((err) => {
        console.error(`Error creating ${type}:`, err);
        updateStatus(`Error creating ${type}: ${err.message}`);
      });
  }

  function dataSendLocalCandidate(ws, id, cand) {
    const { candidate, sdpMid } = cand;
    ws.send(JSON.stringify({ id, type: 'candidate', candidate, mid: sdpMid }));
  }

  function dataRandomId(length) {
    const characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
    const pickRandom = () => characters.charAt(Math.floor(Math.random() * characters.length));
    return [...Array(length)].map(pickRandom).join('');
  }
});

