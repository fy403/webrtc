window.addEventListener('load', () => {
    // =========================================================================
    // References
    // =========================================================================
    const terminalPanel = document.getElementById('shellTerminalPanel');
    const xtermContainer = document.getElementById('terminalXtermContainer');
    const terminalStatus = document.getElementById('shellTerminalStatus');
    const shellStatusDot = document.getElementById('shellStatusDot');
    const shellKillBtn = document.getElementById('shellKillBtn');

    if (!terminalPanel || !xtermContainer) return;

    // =========================================================================
    // Config (with fallback if cmd field missing from old localStorage configs)
    // =========================================================================
    let cmdConfig = ConfigManager.getCmdConfig();
    if (!cmdConfig) {
        console.warn('[CMD] No cmd config found, using defaults');
        cmdConfig = {
            signalingUrl: 'ws://localhost:8000',
            remoteId: '',
            iceServers: [{ urls: ['stun:stun.l.google.com:19302'] }]
        };
    }

    const cmdLocalId = cmdRandomId(5);
    const cmdUrl = `${cmdConfig.signalingUrl}/${cmdLocalId}`;

    const rtcConfig = {
        iceServers: cmdConfig.iceServers || [{
            urls: ['stun:stun.l.google.com:19302']
        }],
    };

    // =========================================================================
    // xterm.js setup
    // =========================================================================
    const term = new Terminal({
        cursorBlink: true,
        cursorStyle: 'block',
        fontSize: 14,
        fontFamily: "'Courier New', 'Consolas', 'Liberation Mono', monospace",
        theme: {
            background: '#0A0E0A',
            foreground: '#C8C8C8',
            cursor: '#32CD32',
            cursorAccent: '#000000',
            selectionBackground: 'rgba(50, 205, 50, 0.3)',
            black: '#1A1A2E',
            red: '#FF5555',
            green: '#32CD32',
            yellow: '#FFAA00',
            blue: '#4EC9B0',
            magenta: '#C586C0',
            cyan: '#00CED1',
            white: '#C8C8C8',
            brightBlack: '#505050',
            brightRed: '#FF6E6E',
            brightGreen: '#50FA7B',
            brightYellow: '#FFD700',
            brightBlue: '#9CDCFE',
            brightMagenta: '#DA70D6',
            brightCyan: '#5CE1E6',
            brightWhite: '#FFFFFF',
        },
        allowProposedApi: true,
        allowTransparency: false,
        scrollback: 5000,
    });

    // ── Inline FitAddon (avoids CDN/CORS issues with module addons) ──
    const fitAddon = {
        _terminal: null,
        activate(t) { this._terminal = t; },
        dispose() { this._terminal = null; },
        fit() {
            const t = this._terminal;
            if (!t || !t.element) return;
            const parent = t.element.parentElement;
            if (!parent) return;
            const style = window.getComputedStyle(parent);
            const availW = parent.clientWidth - parseInt(style.paddingLeft) - parseInt(style.paddingRight) - 2;
            const availH = parent.clientHeight - parseInt(style.paddingTop) - parseInt(style.paddingBottom) - 2;
            if (availW <= 0 || availH <= 0) return;
            try {
                const core = t._core;
                const dims = core._renderService.dimensions;
                const cols = Math.max(2, Math.floor(availW / dims.css.cell.width));
                const rows = Math.max(2, Math.floor(availH / dims.css.cell.height));
                if (t.cols !== cols || t.rows !== rows) t.resize(cols, rows);
            } catch (e) {
                const cols = Math.max(5, Math.floor((availW - 16) / 9.0));
                const rows = Math.max(3, Math.floor((availH - 12) / 17.0));
                t.resize(cols, rows);
            }
        }
    };
    term.loadAddon(fitAddon);

    term.open(xtermContainer);

    // Fit terminal to container
    if (fitAddon) {
        setTimeout(() => { try { fitAddon.fit(); } catch (e) {} }, 100);
    }

    // =========================================================================
    // State
    // =========================================================================
    const cmdPeerConnectionMap = {};
    const cmdDataChannelMap = {};
    let cmdCurrentDataChannel = null;
    let cmdCurrentPeerConnection = null;
    let cmdSignalingWs = null;
    let cmdReconnectInterval = null;
    let cmdWsReconnectInterval = null;
    let cmdRttInterval = null;
    let cmdSessionActive = false;

    // =========================================================================
    // Connection Info Panel Elements
    // =========================================================================
    const cmdLocalIdEl = document.getElementById('cmdLocalId');
    const cmdStatusEl = document.getElementById('cmdStatus');
    const cmdOfferIdEl = document.getElementById('cmdOfferId');
    const cmdOfferBtnEl = document.getElementById('cmdOfferBtn');
    const cmdIceStatusEl = document.getElementById('cmdIceStatus');
    const cmdConnectionTypeEl = document.getElementById('cmdConnectionType');
    const cmdRttEl = document.getElementById('cmdRtt');
    const cmdLocalCandEl = document.getElementById('cmdLocalCandidate');
    const cmdRemoteCandEl = document.getElementById('cmdRemoteCandidate');

    // Display local ID
    if (cmdLocalIdEl) cmdLocalIdEl.textContent = cmdLocalId;

    // Set remote ID from config
    if (cmdOfferIdEl && cmdConfig.remoteId) {
        cmdOfferIdEl.value = cmdConfig.remoteId;
    }

    // Connect button handler
    if (cmdOfferBtnEl) {
        cmdOfferBtnEl.addEventListener('click', () => {
            const remoteId = cmdOfferIdEl ? cmdOfferIdEl.value.trim() : cmdConfig.remoteId;
            if (!remoteId) {
                term.writeln('\x1b[31m[ERROR] Please enter a remote ID\x1b[0m');
                return;
            }
            if (cmdSignalingWs && cmdSignalingWs.readyState === WebSocket.OPEN) {
                cmdUpdateStatus('CONNECTING');
                cmdOfferPeerConnection(cmdSignalingWs, remoteId);
            } else {
                term.writeln('\x1b[31m[ERROR] Signaling not connected, reconnecting...\x1b[0m');
                cmdStartWsReconnect(cmdUrl);
            }
        });
    }

    // =========================================================================
    // Status
    // =========================================================================
    function cmdUpdateStatus(text) {
        if (terminalStatus) terminalStatus.textContent = text;
        if (cmdStatusEl) cmdStatusEl.textContent = text;
        if (shellStatusDot) {
            if (text === 'CONNECTED' || text === 'RUNNING') {
                shellStatusDot.className = 'status-dot dot-green';
            } else if (text === 'CONNECTING') {
                shellStatusDot.className = 'status-dot dot-yellow';
            } else {
                shellStatusDot.className = 'status-dot dot-red';
            }
        }
    }

    function cmdUpdateConnectionInfo(pc) {
        if (!pc) return;
        if (cmdIceStatusEl) cmdIceStatusEl.textContent = pc.iceConnectionState || '--';
        pc.getStats(null).then(stats => {
            stats.forEach(report => {
                if (report.type === 'candidate-pair' && report.nominated) {
                    if (cmdConnectionTypeEl) cmdConnectionTypeEl.textContent = report.localCandidateType || '--';
                    if (cmdLocalCandEl) {
                        const addr = report.localAddress || (report.localCandidateId || '--');
                        cmdLocalCandEl.textContent = addr;
                        cmdLocalCandEl.title = addr;
                    }
                    if (cmdRemoteCandEl) {
                        const addr = report.remoteAddress || (report.remoteCandidateId || '--');
                        cmdRemoteCandEl.textContent = addr;
                        cmdRemoteCandEl.title = addr;
                    }
                }
            });
        }).catch(() => {});
    }

    // RTT measurement
    function cmdStartRttMeasurement(dc) {
        cmdStopRttMeasurement();
        cmdRttInterval = setInterval(() => {
            if (!dc || dc.readyState !== 'open') return;
            const start = performance.now();
            try { dc.send(JSON.stringify({ type: 'ping', t: start })); } catch (e) {}
        }, 2000);
    }

    function cmdStopRttMeasurement() {
        if (cmdRttInterval) { clearInterval(cmdRttInterval); cmdRttInterval = null; }
    }

    // =========================================================================
    // xterm.js event handlers
    // =========================================================================

    // User types → send binary keystrokes to server
    term.onData((data) => {
        if (!cmdCurrentDataChannel || cmdCurrentDataChannel.readyState !== 'open') return;

        // Detect Ctrl+C (0x03): send as shell_kill for extra reliability
        // (the raw byte will also be written to PTY, but having an explicit
        //  signal helps on slow links)
        if (data.length === 1 && data.charCodeAt(0) === 3) {
            try {
                cmdCurrentDataChannel.send(JSON.stringify({
                    type: 'shell_kill', signal: 'SIGINT'
                }));
            } catch (e) {}
        }

        // Send raw bytes as binary DataChannel message
        try {
            const encoder = new TextEncoder();
            const bytes = encoder.encode(data);
            cmdCurrentDataChannel.send(bytes);
        } catch (e) {
            console.error('[CMD] send keystrokes failed:', e);
        }
    });

    // Terminal resize → notify server via JSON
    term.onResize(({ cols, rows }) => {
        if (!cmdCurrentDataChannel || cmdCurrentDataChannel.readyState !== 'open') return;
        try {
            cmdCurrentDataChannel.send(JSON.stringify({
                type: 'shell_resize', rows: rows, cols: cols
            }));
        } catch (e) {}
    });

    // Ctrl+C button at terminal header
    if (shellKillBtn) {
        shellKillBtn.addEventListener('click', () => {
            // Send SIGINT (Ctrl+C)
            if (cmdCurrentDataChannel && cmdCurrentDataChannel.readyState === 'open') {
                try {
                    cmdCurrentDataChannel.send(JSON.stringify({
                        type: 'shell_kill', signal: 'SIGINT'
                    }));
                    // Also send raw Ctrl+C byte to PTY
                    cmdCurrentDataChannel.send(new Uint8Array([3]));
                } catch (e) {}
            }
        });
    }

    // Click anywhere on terminal container → focus terminal
    if (xtermContainer) {
        xtermContainer.addEventListener('click', () => {
            try { term.focus(); } catch (e) {}
        });
    }

    // =========================================================================
    // DataChannel
    // =========================================================================
    function cmdSetupDataChannel(dc, id) {
        dc.binaryType = 'arraybuffer';

        dc.onopen = () => {
            console.log(`CMD DataChannel open with ${id}`);
            cmdCurrentDataChannel = dc;
            cmdSessionActive = true;
            cmdUpdateStatus('CONNECTED');
            cmdStopAutoReconnect();
            cmdStartRttMeasurement(dc);
            if (cmdCurrentPeerConnection) {
                cmdUpdateConnectionInfo(cmdCurrentPeerConnection);
            }

            // Focus terminal on connect
            setTimeout(() => { try { term.focus(); } catch (e) {} }, 200);

            // Fit terminal after connection
            if (fitAddon) {
                setTimeout(() => { try { fitAddon.fit(); } catch (e) {} }, 300);
            }

            term.writeln('\x1b[32m[INFO] Connected. Shell session starting...\x1b[0m');
        };

        dc.onclose = () => {
            console.log(`CMD DataChannel closed with ${id}`);
            if (cmdCurrentDataChannel === dc) {
                cmdCurrentDataChannel = null;
                cmdSessionActive = false;
                cmdStopRttMeasurement();
                cmdUpdateStatus('DISCONNECTED');
                if (cmdIceStatusEl) cmdIceStatusEl.textContent = '--';
                if (cmdConnectionTypeEl) cmdConnectionTypeEl.textContent = '--';
                if (cmdRttEl) cmdRttEl.textContent = '--';
                term.writeln('\x1b[33m[DISCONNECTED] Session closed\x1b[0m');
                if (cmdSignalingWs) {
                    cmdStartAutoReconnect(cmdSignalingWs, id);
                }
            }
        };

        dc.onerror = () => {
            console.error(`CMD DataChannel error with ${id}`);
            term.writeln('\x1b[31m[ERROR] DataChannel error\x1b[0m');
        };

        dc.onmessage = (ev) => {
            // ── binary data = PTY output ──────────────────
            if (ev.data instanceof ArrayBuffer) {
                const bytes = new Uint8Array(ev.data);
                try {
                    term.write(bytes);
                } catch (e) {
                    console.error('[CMD] term.write error:', e);
                }
                return;
            }

            // ── string data = JSON control messages ───────
            if (typeof ev.data !== 'string') return;

            try {
                const msg = JSON.parse(ev.data);

                if (msg.type === 'shell_session') {
                    if (msg.status === 'started') {
                        cmdSessionActive = true;
                        cmdUpdateStatus('RUNNING');
                    } else if (msg.status === 'ended') {
                        cmdSessionActive = false;
                        cmdUpdateStatus('CONNECTED');
                        if (msg.exit_code !== undefined) {
                            const code = msg.exit_code;
                            const color = (code === 0) ? '\x1b[32m' : '\x1b[31m';
                            term.writeln(`${color}[Exit: ${code}]\x1b[0m`);
                        }
                    }
                } else if (msg.type === 'pong' && msg.t !== undefined) {
                    const rtt = Math.round(performance.now() - msg.t);
                    if (cmdRttEl) cmdRttEl.textContent = rtt + ' ms';
                }
            } catch (e) {
                // May be raw text from old protocol – write to terminal
                console.warn('[CMD] non-JSON message received');
            }
        };

        cmdDataChannelMap[id] = dc;
        return dc;
    }

    // =========================================================================
    // WebRTC Connection
    // =========================================================================
    function cmdOfferPeerConnection(ws, id) {
        if (!id) {
            term.writeln('\x1b[31m[ERROR] Missing remote ID\x1b[0m');
            return;
        }
        const pc = cmdCreatePeerConnection(ws, id);
        const dc = pc.createDataChannel('cmd');
        cmdSetupDataChannel(dc, id);
        cmdUpdateStatus('CONNECTING');
        cmdSendLocalDescription(ws, id, pc, 'offer');
    }

    function cmdCreatePeerConnection(ws, id) {
        const pc = new RTCPeerConnection(rtcConfig);
        cmdCurrentPeerConnection = pc;

        pc.oniceconnectionstatechange = () => {
            if (cmdIceStatusEl) cmdIceStatusEl.textContent = pc.iceConnectionState || '--';
            if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
                cmdStopAutoReconnect();
                cmdUpdateConnectionInfo(pc);
            } else if (pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'disconnected') {
                if (cmdIceStatusEl) cmdIceStatusEl.textContent = '--';
                if (cmdConnectionTypeEl) cmdConnectionTypeEl.textContent = '--';
                if (cmdRttEl) cmdRttEl.textContent = '--';
            }
        };

        pc.onconnectionstatechange = () => {
            if (pc.connectionState === 'connected') {
                cmdStopAutoReconnect();
            } else if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected') {
                cmdUpdateStatus('DISCONNECTED');
                cmdStartAutoReconnect(ws, id);
            }
        };

        pc.onicecandidate = (event) => {
            if (event.candidate) cmdSendLocalCandidate(ws, id, event.candidate);
        };

        cmdPeerConnectionMap[id] = pc;
        return pc;
    }

    // =========================================================================
    // Signaling
    // =========================================================================
    function cmdOpenSignaling(url) {
        return new Promise((resolve, reject) => {
            const ws = new WebSocket(url);
            cmdSignalingWs = ws;

            ws.onopen = () => {
                console.log('CMD Signaling WebSocket connected');
                resolve(ws);
            };

            ws.onerror = () => reject(new Error('CMD WebSocket error'));

            ws.onclose = () => {
                console.error('CMD Signaling WebSocket disconnected');
                cmdSignalingWs = null;
                cmdStartWsReconnect(url);
            };

            ws.onmessage = (e) => {
                if (typeof e.data !== 'string') return;
                const message = JSON.parse(e.data);
                const {id, type} = message;

                let pc = cmdPeerConnectionMap[id];
                if (!pc) {
                    if (type !== 'offer') return;
                    pc = cmdCreatePeerConnection(ws, id);
                }

                switch (type) {
                    case 'offer':
                    case 'answer':
                        pc.setRemoteDescription({sdp: message.description, type: message.type})
                            .then(() => {
                                if (type === 'offer') {
                                    cmdSendLocalDescription(ws, id, pc, 'answer');
                                }
                            })
                            .catch((err) => console.error(`CMD Error setting remote ${type}:`, err));
                        break;
                    case 'candidate':
                        pc.addIceCandidate({candidate: message.candidate, sdpMid: message.mid});
                        break;
                }
            };
        });
    }

    // =========================================================================
    // SDP and ICE helpers
    // =========================================================================
    function cmdSendLocalDescription(ws, id, pc, type) {
        (type === 'offer' ? pc.createOffer() : pc.createAnswer())
            .then((desc) => pc.setLocalDescription(desc))
            .then(() => {
                const {sdp, type} = pc.localDescription;
                ws.send(JSON.stringify({id, type, description: sdp}));
            })
            .catch((err) => console.error(`CMD Error creating ${type}:`, err));
    }

    function cmdSendLocalCandidate(ws, id, cand) {
        ws.send(JSON.stringify({id, type: 'candidate', candidate: cand.candidate, mid: cand.sdpMid}));
    }

    // =========================================================================
    // Auto-reconnect
    // =========================================================================
    function cmdStartAutoReconnect(ws, id) {
        cmdStopAutoReconnect();
        cmdUpdateStatus('RECONNECTING');
        cmdReconnectInterval = setInterval(() => {
            if (!ws || ws.readyState !== WebSocket.OPEN) return;
            if (cmdPeerConnectionMap[id]) {
                cmdPeerConnectionMap[id].close();
                delete cmdPeerConnectionMap[id];
            }
            cmdOfferPeerConnection(ws, id);
        }, 2000);
    }

    function cmdStopAutoReconnect() {
        if (cmdReconnectInterval) { clearInterval(cmdReconnectInterval); cmdReconnectInterval = null; }
    }

    function cmdStartWsReconnect(url) {
        cmdStopWsReconnect();
        cmdWsReconnectInterval = setInterval(() => {
            cmdOpenSignaling(url)
                .then((ws) => {
                    cmdStopWsReconnect();
                    const remoteId = cmdOfferIdEl ? cmdOfferIdEl.value.trim() : cmdConfig.remoteId;
                    if (remoteId) {
                        setTimeout(() => cmdOfferPeerConnection(ws, remoteId), 1000);
                    }
                })
                .catch((err) => console.error('CMD reconnection failed:', err.message));
        }, 3000);
    }

    function cmdStopWsReconnect() {
        if (cmdWsReconnectInterval) { clearInterval(cmdWsReconnectInterval); cmdWsReconnectInterval = null; }
    }

    // =========================================================================
    // Cleanup
    // =========================================================================
    window.addEventListener('beforeunload', () => {
        cmdStopAutoReconnect();
        cmdStopWsReconnect();
        cmdStopRttMeasurement();
        try { term.dispose(); } catch (e) {}
    });

    // =========================================================================
    // Utility
    // =========================================================================
    function cmdRandomId(length) {
        const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
        const pick = () => chars.charAt(Math.floor(Math.random() * chars.length));
        return [...Array(length)].map(pick).join('');
    }

    // =========================================================================
    // Initialize
    // =========================================================================
    cmdUpdateStatus('DISCONNECTED');
    term.writeln('\x1b[32m╔══════════════════════════════════════════╗\x1b[0m');
    term.writeln('\x1b[32m║     CmdTrack Interactive Shell v2.0     ║\x1b[0m');
    term.writeln('\x1b[32m║   Support: htop, vim, tmux, bash, ...   ║\x1b[0m');
    term.writeln('\x1b[32m╚══════════════════════════════════════════╝\x1b[0m');
    term.writeln('\x1b[36mWaiting for WebRTC connection...\x1b[0m');

    // Focus terminal on load
    setTimeout(() => { try { term.focus(); } catch (e) {} }, 600);

    // Terminal card click → focus
    const terminalCard = document.querySelector('.terminal-card');
    if (terminalCard) {
        terminalCard.addEventListener('click', (e) => {
            if (e.target.tagName === 'BUTTON') return;
            try { term.focus(); } catch (e) {}
        });
    }

    console.log('CMD: Connecting to signaling...');
    cmdOpenSignaling(cmdUrl)
        .then((ws) => {
            console.log('CMD: Signaling connected');
            cmdStopWsReconnect();
            const remoteId = cmdOfferIdEl ? cmdOfferIdEl.value.trim() : cmdConfig.remoteId;
            if (remoteId) {
                setTimeout(() => {
                    cmdUpdateStatus('CONNECTING');
                    cmdOfferPeerConnection(ws, remoteId);
                }, 1000);
            }
        })
        .catch((err) => {
            console.error('CMD: Signaling connection failed:', err);
            cmdUpdateStatus('DISCONNECTED');
            cmdStartWsReconnect(cmdUrl);
        });
});
