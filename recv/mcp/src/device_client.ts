// =============================================================================
// DeviceClient - WebRTC DataChannel client for remote device control
// Uses werift (pure TS WebRTC) + ws (WebSocket signaling)
// Reference: web/data_rtc.js + data_track/src/main.cpp
// =============================================================================

import * as werift from 'werift';
import { WebSocket } from 'ws';
import { EventEmitter } from 'node:events';
import {
  getConfig,
  randomId,
  type ServerConfig,
} from './config.js';
import {
  encodeControlFrame,
  encodeHeartbeatFrame,
  encodeVideoConfigMessage,
  encodeCommandMessage,
  createNeutralFrame,
  type ControlFrame,
  CHANNELS,
  PWM_NEUTRAL,
} from './protocol.js';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface DeviceStatus {
  timestamp: number;
  connection_count: number;
  rx_speed: number;
  tx_speed: number;
  cpu_usage: number;
  cpu_core_count: number;
  cpu_load_1min: number;
  cpu_load_5min: number;
  cpu_load_15min: number;
  mem_total_mb: number;
  mem_used_mb: number;
  mem_free_mb: number;
  mem_usage: number;
  signal_4g: string;
  sim_status: string;
  network: string;
  module_info: string;
  gps_latitude: number;
  gps_longitude: number;
  gps_altitude: number;
  gps_quality: number;
  gps_satellites: number;
  gps_speed_kmh: number;
  gps_time: string;
  [key: string]: unknown;
}

export type ConnectionState =
  | 'disconnected'
  | 'connecting_signaling'
  | 'connected_signaling'
  | 'connecting_peer'
  | 'connected'
  | 'reconnecting'
  | 'failed';

// ---------------------------------------------------------------------------
// DeviceClient class
// ---------------------------------------------------------------------------

export class DeviceClient extends EventEmitter {
  private config: ServerConfig;
  private localId: string;
  private ws: WebSocket | null = null;
  private pc: werift.RTCPeerConnection | null = null;
  private dc: werift.RTCDataChannel | null = null;

  private connectionState: ConnectionState = 'disconnected';
  private sequence: number = 0;
  private statusBuffer: DeviceStatus | null = null;
  private lastStatusTime: number = 0;

  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private wsReconnectTimer: ReturnType<typeof setInterval> | null = null;
  private peerReconnectTimer: ReturnType<typeof setInterval> | null = null;
  private connectionTimeout: ReturnType<typeof setTimeout> | null = null;

  private targetRemoteId: string = '';

  constructor(configOverride?: Partial<ServerConfig>) {
    super();
    this.config = getConfig();
    if (configOverride) {
      this.config = { ...this.config, ...configOverride };
    }
    this.localId = 'mcp_' + randomId(6);
  }

  // -----------------------------------------------------------------------
  // Public API
  // -----------------------------------------------------------------------

  getLocalId(): string {
    return this.localId;
  }

  getState(): ConnectionState {
    return this.connectionState;
  }

  isConnected(): boolean {
    return this.connectionState === 'connected' && this.dc !== null && this.dc.readyState === 'open';
  }

  getLastStatus(): DeviceStatus | null {
    return this.statusBuffer;
  }

  getLastStatusTime(): number {
    return this.lastStatusTime;
  }

  /**
   * Connect to a remote device by ID
   */
  async connect(remoteId: string): Promise<string> {
    if (!remoteId || remoteId.trim() === '') {
      throw new Error('remoteId is required');
    }
    this.targetRemoteId = remoteId;
    this.config.remoteId = remoteId;
    return this._connectSignaling();
  }

  /**
   * Disconnect from remote device
   */
  disconnect(): void {
    this.targetRemoteId = '';
    this._cleanup();
    this.connectionState = 'disconnected';
    this.emit('stateChange', this.connectionState);
    this.emit('disconnected');
  }

  /**
   * Send control frame (16-channel PWM values)
   */
  sendControl(frame: ControlFrame): void {
    if (!this.dc || this.dc.readyState !== 'open') {
      throw new Error('DataChannel not open');
    }
    const data = encodeControlFrame(frame);
    this.dc.send(data);
    this.sequence = frame.sequence + 1;
  }

  /**
   * Send emergency stop (all channels to neutral 1500)
   */
  sendStop(): void {
    if (!this.dc || this.dc.readyState !== 'open') {
      throw new Error('DataChannel not open');
    }
    const frame = createNeutralFrame(this.sequence++);
    this.dc.send(encodeControlFrame(frame));
  }

  /**
   * Send video configuration
   */
  sendVideoConfig(fps: number, bitrate: number): void {
    if (!this.dc || this.dc.readyState !== 'open') {
      throw new Error('DataChannel not open');
    }
    this.dc.send(encodeVideoConfigMessage({ fps, bitrate }));
  }

  /**
   * Send arbitrary JSON command for extensibility
   */
  sendCommand(command: Record<string, unknown>): void {
    if (!this.dc || this.dc.readyState !== 'open') {
      throw new Error('DataChannel not open');
    }
    this.dc.send(encodeCommandMessage(command));
  }

  // -----------------------------------------------------------------------
  // Internal: Signaling Connection
  // -----------------------------------------------------------------------

  private _connectSignaling(): Promise<string> {
    return new Promise((resolve, reject) => {
      this.connectionState = 'connecting_signaling';
      this.emit('stateChange', this.connectionState);

      const wsUrl = `ws://${this.config.signalingUrl}:${this.config.signalingPort}/${this.localId}`;
      console.error(`[device] Connecting to signaling: ${wsUrl}`);

      const ws = new WebSocket(wsUrl);
      this.ws = ws;

      const settled = { done: false };
      const finish = (err: Error | null, result?: string) => {
        if (settled.done) return;
        settled.done = true;
        if (err) {
          this.connectionState = 'failed';
          this.emit('stateChange', this.connectionState);
          reject(err);
        } else {
          resolve(result!);
        }
      };

      ws.on('open', () => {
        console.error('[device] Signaling WebSocket connected');
        this.connectionState = 'connected_signaling';
        this.emit('stateChange', this.connectionState);
        this._stopWsReconnect();
        finish(null, 'signaling_connected');

        // Auto-connect to remote if configured
        if (this.targetRemoteId) {
          setTimeout(() => this._connectPeer(), 500);
        }
      });

      ws.on('error', (err: Error) => {
        console.error('[device] WebSocket error:', err.message);
        finish(new Error(`WebSocket error: ${err.message}`));
      });

      ws.on('close', () => {
        console.error('[device] WebSocket closed');
        if (this.ws === ws) {
          this.ws = null;
        }
        if (this.connectionState !== 'disconnected') {
          this.connectionState = 'disconnected';
          this.emit('stateChange', this.connectionState);
          this.emit('signalingClosed');
        }
        if (this.targetRemoteId && this.config.autoReconnect) {
          this._startWsReconnect();
        }
      });

      ws.on('message', (data: Buffer) => {
        const text = data.toString('utf-8');
        this._handleSignalingMessage(text);
      });
    });
  }

  // -----------------------------------------------------------------------
  // Internal: Peer Connection
  // -----------------------------------------------------------------------

  private _connectPeer(): void {
    if (!this.ws || !this.targetRemoteId) return;

    this.connectionState = 'connecting_peer';
    this.emit('stateChange', this.connectionState);
    console.error(`[device] Connecting to peer: ${this.targetRemoteId}`);

    // Clean old connection
    if (this.pc) {
      try { this.pc.close(); } catch (_) { /* ignore */ }
      this.pc = null;
    }
    if (this.dc) {
      this.dc = null;
    }

    // Build ICE config (werift expects urls: string, not string[])
    const iceServers: werift.RTCIceServer[] = [
      { urls: `stun:${this.config.stunServer}:${this.config.stunPort}` },
    ];

    if (this.config.turnServer) {
      iceServers.push({
        urls: `turn:${this.config.turnServer}:${this.config.turnPort}?transport=udp`,
        username: this.config.turnUser,
        credential: this.config.turnPass,
      });
    }

    const pc = new werift.RTCPeerConnection({
      iceServers,
      bundlePolicy: 'max-bundle',
    });
    this.pc = pc;

    // --- PeerConnection event handlers ---

    pc.onconnectionstatechange = () => {
      const state = pc.connectionState;
      console.error(`[device] PC connection state: ${state}`);

      if (state === 'connected') {
        this.connectionState = 'connected';
        this.emit('stateChange', this.connectionState);
        this.emit('connected');
        this._stopPeerReconnect();
        this._clearConnectionTimeout();
      } else if (state === 'failed') {
        this.emit('peerFailed');
        if (this.targetRemoteId && this.config.autoReconnect) {
          setTimeout(() => this._startPeerReconnect(), 500);
        }
      } else if (state === 'disconnected') {
        this.emit('peerDisconnected');
        if (this.targetRemoteId && this.config.autoReconnect) {
          setTimeout(() => {
            if (this.connectionState !== 'connected') {
              this._startPeerReconnect();
            }
          }, 1000);
        }
      } else if (state === 'closed') {
        if (this.pc === pc) this.pc = null;
      }
    };

    pc.oniceconnectionstatechange = () => {
      console.error(`[device] ICE state: ${pc.iceConnectionState}`);
    };

    pc.onicecandidate = (event) => {
      if (event.candidate && this.ws) {
        this.ws.send(
          JSON.stringify({
            id: this.targetRemoteId,
            type: 'candidate',
            candidate: event.candidate.candidate,
            mid: event.candidate.sdpMid,
            sdpMLineIndex: event.candidate.sdpMLineIndex,
          })
        );
      }
    };

    pc.ondatachannel = (event) => {
      console.error(`[device] Incoming DataChannel: "${event.channel.label}"`);
      this._setupDataChannel(event.channel);
    };

    // Create and setup DataChannel
    const dc = pc.createDataChannel('control', {
      ordered: true,
      maxRetransmits: 10,
    });
    this._setupDataChannel(dc);

    // Create and send offer
    this._createAndSendOffer(pc);

    // Connection timeout
    this._clearConnectionTimeout();
    this.connectionTimeout = setTimeout(() => {
      if (this.pc === pc && this.connectionState === 'connecting_peer') {
        console.error('[device] Connection timeout');
        try { pc.close(); } catch (_) { /* ignore */ }
        if (this.pc === pc) this.pc = null;
        this.connectionState = 'disconnected';
        this.emit('stateChange', this.connectionState);
      }
    }, 10000);
  }

  private async _createAndSendOffer(pc: werift.RTCPeerConnection): Promise<void> {
    try {
      const offer = await (pc.createOffer as any)();
      await pc.setLocalDescription(offer);

      if (pc.localDescription && this.ws) {
        this.ws.send(
          JSON.stringify({
            id: this.targetRemoteId,
            type: pc.localDescription.type,
            description: pc.localDescription.sdp,
          })
        );
      }
    } catch (err) {
      console.error('[device] Failed to create offer:', err);
    }
  }

  private _setupDataChannel(dc: werift.RTCDataChannel): void {
    this.dc = dc;

    // Enable binary mode (werift handles this internally)
    (dc as any).binaryType = 'arraybuffer';

    dc.onopen = () => {
      console.error('[device] DataChannel open');
      this.connectionState = 'connected';
      this.emit('stateChange', this.connectionState);
      this.emit('connected');
      this._stopPeerReconnect();

      // Send neutral frame on connect (safety)
      const neutral = createNeutralFrame(this.sequence++);
      try { dc.send(encodeControlFrame(neutral)); } catch (_) { /* ignore */ }

      // Start heartbeat
      this._startHeartbeat();
    };

    dc.onclose = () => {
      console.error('[device] DataChannel closed');
      if (this.dc === dc) this.dc = null;
      this._stopHeartbeat();
      this.emit('disconnected');
      if (this.targetRemoteId && this.config.autoReconnect) {
        setTimeout(() => this._startPeerReconnect(), 500);
      }
    };

    dc.onerror = (_event: any) => {
      console.error('[device] DataChannel error');
    };

    dc.onmessage = (event: any) => {
      const msg = event.data;
      if (typeof msg === 'string') {
        this._handleTextMessage(msg);
      } else if (msg instanceof ArrayBuffer || msg instanceof Buffer) {
        this._handleBinaryMessage(new Uint8Array(msg as ArrayBuffer));
      } else if (msg instanceof Uint8Array) {
        this._handleBinaryMessage(msg);
      }
    };
  }

  // -----------------------------------------------------------------------
  // Internal: Signaling message handling
  // -----------------------------------------------------------------------

  private _handleSignalingMessage(data: string): void {
    try {
      const msg = JSON.parse(data);
      const { id, type } = msg;

      if (!id || !type) return;

      if (id !== this.targetRemoteId) {
        return; // Message for a different peer
      }

      switch (type) {
        case 'offer': {
          if (!this.pc) {
            this._connectPeer();
          }
          if (this.pc) {
            const desc: any = { sdp: msg.description, type: 'offer' };
            this.pc.setRemoteDescription(desc).then(async () => {
              const answer = await this.pc!.createAnswer();
              await this.pc!.setLocalDescription(answer);
              if (this.pc!.localDescription && this.ws) {
                this.ws.send(
                  JSON.stringify({
                    id: this.targetRemoteId,
                    type: 'answer',
                    description: this.pc!.localDescription.sdp,
                  })
                );
              }
            }).catch((err) => {
              console.error('[device] Error handling offer:', err);
            });
          }
          break;
        }
        case 'answer': {
          if (this.pc) {
            const desc: any = { sdp: msg.description, type: 'answer' };
            this.pc.setRemoteDescription(desc).catch((err) => {
              console.error('[device] Error setting remote answer:', err);
            });
          }
          break;
        }
        case 'candidate': {
          if (this.pc) {
            const candidate = new werift.RTCIceCandidate({
              candidate: msg.candidate,
              sdpMid: msg.mid,
              sdpMLineIndex: msg.sdpMLineIndex ?? 0,
            });
            this.pc.addIceCandidate(candidate).catch((err) => {
              console.error('[device] Error adding ICE candidate:', err);
            });
          }
          break;
        }
        case 'peer_close': {
          console.error(`[device] Received peer_close from ${id}`);
          this.emit('peerClose');
          if (this.dc) {
            this.dc = null;
          }
          break;
        }
        case 'ping': {
          // Ignore
          break;
        }
      }
    } catch (err) {
      console.error('[device] Failed to parse signaling message:', err);
    }
  }

  // -----------------------------------------------------------------------
  // Internal: DataChannel message handling
  // -----------------------------------------------------------------------

  private _handleTextMessage(data: string): void {
    try {
      const msg = JSON.parse(data);
      if (msg.rx_speed !== undefined || msg.cpu_usage !== undefined) {
        this.statusBuffer = this._normalizeStatus(msg);
        this.lastStatusTime = Date.now();
        this.emit('statusUpdate', this.statusBuffer);
      } else {
        this.emit('message', msg);
      }
    } catch {
      this.emit('rawMessage', data);
    }
  }

  private _handleBinaryMessage(data: Uint8Array): void {
    try {
      const text = new TextDecoder().decode(data);
      const msg = JSON.parse(text);
      if (msg.rx_speed !== undefined || msg.cpu_usage !== undefined) {
        this.statusBuffer = this._normalizeStatus(msg);
        this.lastStatusTime = Date.now();
        this.emit('statusUpdate', this.statusBuffer);
      } else {
        this.emit('message', msg);
      }
    } catch {
      // Non-JSON binary, ignore
    }
  }

  private _normalizeStatus(raw: Record<string, string | number | undefined>): DeviceStatus {
    const parseNum = (v: unknown, scale: number = 1): number => {
      const n = typeof v === 'string' ? parseFloat(v) : (typeof v === 'number' ? v : 0);
      return isNaN(n) ? 0 : n / scale;
    };

    const parseStr = (v: unknown): string =>
      typeof v === 'string' ? v : (v !== undefined ? String(v) : 'N/A');

    return {
      timestamp: parseNum(raw.timestamp, 1),
      connection_count: parseNum(raw.connection_count, 1),
      rx_speed: parseNum(raw.rx_speed, 100),
      tx_speed: parseNum(raw.tx_speed, 100),
      cpu_usage: parseNum(raw.cpu_usage, 100),
      cpu_core_count: parseNum(raw.cpu_core_count, 1),
      cpu_load_1min: parseNum(raw.cpu_load_1min, 100),
      cpu_load_5min: parseNum(raw.cpu_load_5min, 100),
      cpu_load_15min: parseNum(raw.cpu_load_15min, 100),
      mem_total_mb: parseNum(raw.mem_total_mb, 1),
      mem_used_mb: parseNum(raw.mem_used_mb, 1),
      mem_free_mb: parseNum(raw.mem_free_mb, 1),
      mem_usage: parseNum(raw.mem_usage, 1),
      signal_4g: parseStr(raw['4g_signal']),
      sim_status: parseStr(raw.sim_status),
      network: parseStr(raw.network),
      module_info: parseStr(raw.module_info),
      gps_latitude: parseNum(raw.gps_latitude, 1),
      gps_longitude: parseNum(raw.gps_longitude, 1),
      gps_altitude: parseNum(raw.gps_altitude, 1),
      gps_quality: parseNum(raw.gps_quality, 1),
      gps_satellites: parseNum(raw.gps_satellites, 1),
      gps_speed_kmh: parseNum(raw.gps_speed_kmh, 1),
      gps_time: parseStr(raw.gps_time),
    };
  }

  // -----------------------------------------------------------------------
  // Internal: Heartbeat
  // -----------------------------------------------------------------------

  private _startHeartbeat(): void {
    this._stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (this.dc && this.dc.readyState === 'open') {
        try {
          this.dc.send(encodeHeartbeatFrame(this.sequence++));
        } catch (_) { /* ignore */ }
      }
    }, this.config.heartbeatIntervalMs);
  }

  private _stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  // -----------------------------------------------------------------------
  // Internal: Reconnect logic
  // -----------------------------------------------------------------------

  private _startWsReconnect(): void {
    this._stopWsReconnect();
    console.error('[device] Starting WS reconnect...');
    this.connectionState = 'reconnecting';
    this.emit('stateChange', this.connectionState);
    this.wsReconnectTimer = setInterval(() => {
      this._connectSignaling().catch((err) => {
        console.error('[device] WS reconnect failed:', err.message);
      });
    }, this.config.reconnectIntervalMs);
  }

  private _stopWsReconnect(): void {
    if (this.wsReconnectTimer) {
      clearInterval(this.wsReconnectTimer);
      this.wsReconnectTimer = null;
    }
  }

  private _startPeerReconnect(): void {
    this._stopPeerReconnect();
    this.connectionState = 'reconnecting';
    this.emit('stateChange', this.connectionState);
    console.error('[device] Starting peer reconnect...');
    this.peerReconnectTimer = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this._connectPeer();
      }
    }, this.config.reconnectIntervalMs);
  }

  private _stopPeerReconnect(): void {
    if (this.peerReconnectTimer) {
      clearInterval(this.peerReconnectTimer);
      this.peerReconnectTimer = null;
    }
  }

  private _clearConnectionTimeout(): void {
    if (this.connectionTimeout) {
      clearTimeout(this.connectionTimeout);
      this.connectionTimeout = null;
    }
  }

  // -----------------------------------------------------------------------
  // Internal: Cleanup
  // -----------------------------------------------------------------------

  private _cleanup(): void {
    this._stopHeartbeat();
    this._stopWsReconnect();
    this._stopPeerReconnect();
    this._clearConnectionTimeout();

    // Send peer_close via signaling
    if (this.ws && this.ws.readyState === WebSocket.OPEN && this.targetRemoteId) {
      try {
        this.ws.send(
          JSON.stringify({ id: this.targetRemoteId, type: 'peer_close' })
        );
      } catch (_) { /* ignore */ }
    }

    // Close DataChannel
    if (this.dc) {
      try { this.dc.close(); } catch (_) { /* ignore */ }
      this.dc = null;
    }

    // Close PeerConnection
    if (this.pc) {
      try { this.pc.close(); } catch (_) { /* ignore */ }
      this.pc = null;
    }

    // Close WebSocket
    if (this.ws) {
      try { this.ws.close(); } catch (_) { /* ignore */ }
      this.ws = null;
    }
  }
}

// Singleton
let _instance: DeviceClient | null = null;

export function getDeviceClient(): DeviceClient {
  if (!_instance) {
    _instance = new DeviceClient();
  }
  return _instance;
}

export function resetDeviceClient(): void {
  if (_instance) {
    _instance.disconnect();
    _instance.removeAllListeners();
    _instance = null;
  }
}
