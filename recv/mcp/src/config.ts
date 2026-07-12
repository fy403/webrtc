// =============================================================================
// Configuration Manager - loads config.json with CLI/env overrides
// =============================================================================

import * as fs from 'node:fs';
import * as path from 'node:path';

export interface ServerConfig {
  signalingUrl: string;
  signalingPort: number;
  stunServer: string;
  stunPort: number;
  turnServer: string;
  turnPort: number;
  turnUser: string;
  turnPass: string;
  remoteId: string;
  autoReconnect: boolean;
  reconnectIntervalMs: number;
  controlTimeoutMs: number;
  heartbeatIntervalMs: number;
}

const DEFAULT_CONFIG: ServerConfig = {
  signalingUrl: '119.45.178.251',
  signalingPort: 8000,
  stunServer: 'stun.l.google.com',
  stunPort: 19302,
  turnServer: '119.45.178.251',
  turnPort: 3478,
  turnUser: 'fy403',
  turnPass: 'qwertyuiop',
  remoteId: '',
  autoReconnect: true,
  reconnectIntervalMs: 3000,
  controlTimeoutMs: 500,
  heartbeatIntervalMs: 300,
};

let _config: ServerConfig = { ...DEFAULT_CONFIG };

export function getConfig(): ServerConfig {
  return _config;
}

export function updateConfig(partial: Partial<ServerConfig>): void {
  _config = { ..._config, ...partial };
}

/**
 * Load config from file, with env variable overrides
 */
export function loadConfig(configPath?: string): ServerConfig {
  const resolvedPath = configPath || path.join(process.cwd(), 'config.json');

  // Load from JSON file if exists
  if (fs.existsSync(resolvedPath)) {
    try {
      const raw = fs.readFileSync(resolvedPath, 'utf-8');
      const fileConfig = JSON.parse(raw);
      _config = { ...DEFAULT_CONFIG, ...fileConfig };
    } catch (err) {
      console.error(`[config] Failed to load ${resolvedPath}, using defaults:`, err);
      _config = { ...DEFAULT_CONFIG };
    }
  } else {
    _config = { ...DEFAULT_CONFIG };
  }

  // Environment variable overrides
  const envOverrides: Partial<ServerConfig> = {};
  if (process.env.SIGNALING_URL) envOverrides.signalingUrl = process.env.SIGNALING_URL;
  if (process.env.SIGNALING_PORT) envOverrides.signalingPort = parseInt(process.env.SIGNALING_PORT, 10);
  if (process.env.REMOTE_ID) envOverrides.remoteId = process.env.REMOTE_ID;
  if (process.env.STUN_SERVER) envOverrides.stunServer = process.env.STUN_SERVER;
  if (process.env.TURN_SERVER) envOverrides.turnServer = process.env.TURN_SERVER;
  if (process.env.TURN_USER) envOverrides.turnUser = process.env.TURN_USER;
  if (process.env.TURN_PASS) envOverrides.turnPass = process.env.TURN_PASS;

  _config = { ..._config, ...envOverrides };
  return _config;
}

/**
 * Generate random client ID (matching data_rtc.js dataRandomId)
 */
export function randomId(length: number = 5): string {
  const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
  let id = '';
  for (let i = 0; i < length; i++) {
    id += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return id;
}
