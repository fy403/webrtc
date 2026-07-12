// =============================================================================
// MCP Server Entry Point
// Reference: MCP SDK + mcp_best_practices.md
// =============================================================================

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { loadConfig, updateConfig, getConfig } from './config.js';
import {
  getDeviceClient,
  resetDeviceClient,
  type DeviceClient,
  type DeviceStatus,
} from './device_client.js';
import {
  createControlFrame,
  createNeutralFrame,
  CHANNELS,
  PWM_MIN,
  PWM_MAX,
  PWM_NEUTRAL,
} from './protocol.js';

// ===========================================================================
// Server Initialization
// ===========================================================================

const server = new McpServer({
  name: 'remote-control-mcp-server',
  version: '1.0.0',
});

// Load config on startup
loadConfig();

const config = getConfig();
console.error(`[mcp] Config loaded: signaling=${config.signalingUrl}:${config.signalingPort}`);

// ===========================================================================
// Helper: get or create device client
// ===========================================================================

let client: DeviceClient | null = null;

function ensureClient(): DeviceClient {
  if (!client) {
    client = getDeviceClient();
  }
  return client;
}

function formatStatus(status: DeviceStatus | null): string {
  if (!status) {
    return 'No status data available yet. Device may be disconnected or not sending telemetry.';
  }

  const memPercent = status.mem_total_mb > 0
    ? ((status.mem_used_mb / status.mem_total_mb) * 100).toFixed(1)
    : '0';

  return [
    '## Remote Device Status',
    '',
    `| Metric | Value |`,
    `|--------|-------|`,
    `| CPU Usage | ${status.cpu_usage.toFixed(1)}% (${status.cpu_core_count} cores) |`,
    `| CPU Load | 1m: ${status.cpu_load_1min.toFixed(1)}% / 5m: ${status.cpu_load_5min.toFixed(1)}% / 15m: ${status.cpu_load_15min.toFixed(1)}% |`,
    `| Memory | ${status.mem_used_mb} MB / ${status.mem_total_mb} MB (${memPercent}%) |`,
    `| Network RX | ${status.rx_speed.toFixed(1)} Kbps |`,
    `| Network TX | ${status.tx_speed.toFixed(1)} Kbps |`,
    `| Connections | ${status.connection_count} |`,
    '',
    '### 4G Module',
    `| Metric | Value |`,
    `|--------|-------|`,
    `| Signal | ${status.signal_4g} |`,
    `| SIM | ${status.sim_status} |`,
    `| Network | ${status.network} |`,
    '',
    '### GPS',
    `| Metric | Value |`,
    `|--------|-------|`,
    `| Latitude | ${status.gps_latitude.toFixed(6)}° |`,
    `| Longitude | ${status.gps_longitude.toFixed(6)}° |`,
    `| Altitude | ${status.gps_altitude.toFixed(1)} m |`,
    `| Speed | ${status.gps_speed_kmh.toFixed(1)} km/h |`,
    `| Satellites | ${status.gps_satellites} |`,
    `| Quality | ${status.gps_quality} (0=invalid, 1=GPS, 2=DGPS) |`,
    '',
  ].join('\n');
}

// ===========================================================================
// Tool: device_connect
// ===========================================================================

server.registerTool(
  'device_connect',
  {
    title: 'Connect to Remote Device',
    description:
      'Establish a WebRTC DataChannel connection to a remote device via ' +
      'the signaling server. The remoteId identifies the target device ' +
      '(must match the client_id configured in data_track/config.txt). ' +
      'After connecting, you can send control frames, query status, and ' +
      'configure video parameters.',
    inputSchema: {
      remoteId: z
        .string()
        .min(1)
        .max(64)
        .describe(
          'The remote device ID to connect to. Must match the client_id of ' +
          'the data_track service running on the remote device.'
        ),
      autoReconnect: z
        .boolean()
        .optional()
        .default(true)
        .describe('Whether to auto-reconnect on disconnection (default: true)'),
    },
    annotations: {
      readOnlyHint: false,
      destructiveHint: false,
      idempotentHint: false,
      openWorldHint: true,
    },
  },
  async (params) => {
    try {
      const devClient = ensureClient();
      updateConfig({ autoReconnect: params.autoReconnect ?? true, remoteId: params.remoteId });

      const result = await devClient.connect(params.remoteId);

      return {
        content: [
          {
            type: 'text' as const,
            text: `Successfully connected to signaling server. Local ID: ${devClient.getLocalId()}. Attempting P2P connection to remote device "${params.remoteId}"...`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to connect: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_disconnect
// ===========================================================================

server.registerTool(
  'device_disconnect',
  {
    title: 'Disconnect from Remote Device',
    description:
      'Cleanly disconnect from the remote device. Closes the DataChannel, ' +
      'PeerConnection, and WebSocket signaling connection. Sends peer_close ' +
      'notification to the signaling server.',
    inputSchema: {},
    annotations: {
      readOnlyHint: false,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: true,
    },
  },
  async () => {
    try {
      resetDeviceClient();
      client = null;
      return {
        content: [
          { type: 'text' as const, text: 'Disconnected from remote device. All connections closed.' },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Error during disconnect: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_send_control
// ===========================================================================

const channelField = z
  .number()
  .min(PWM_MIN)
  .max(PWM_MAX)
  .describe(`PWM value in microseconds (${PWM_MIN}-${PWM_MAX}, neutral=${PWM_NEUTRAL})`);

server.registerTool(
  'device_send_control',
  {
    title: 'Send 16-Channel Control Frame',
    description:
      'Send a full 16-channel RC control frame to the remote device using ' +
      'RCProtocolV2 binary format. Each channel accepts a PWM value in ' +
      'microseconds (1000~2000, where 1500 is neutral/center). ' +
      'Channel assignments depend on the remote device configuration:\n' +
      '- CH1 (index 0): Typically throttle/forward\n' +
      '- CH2 (index 1): Typically steering/turn\n' +
      '- CH3-CH16: Auxiliary channels (switches, gimbals, etc.)\n\n' +
      'IMPORTANT: Always call `device_stop` to return to neutral when done. ' +
      'Control frames have a 500ms timeout on the remote side - if no frames ' +
      'are received within that window, the device will auto-stop for safety.',
    inputSchema: {
      ch1: channelField.optional().describe('CH1: typically throttle (1000=reverse, 1500=stop, 2000=forward)'),
      ch2: channelField.optional().describe('CH2: typically steering (1000=left, 1500=center, 2000=right)'),
      ch3: channelField.optional().describe('CH3: auxiliary'),
      ch4: channelField.optional().describe('CH4: auxiliary'),
      ch5: channelField.optional().describe('CH5: auxiliary'),
      ch6: channelField.optional().describe('CH6: auxiliary'),
      ch7: channelField.optional().describe('CH7: auxiliary'),
      ch8: channelField.optional().describe('CH8: auxiliary'),
      ch9: channelField.optional().describe('CH9: auxiliary'),
      ch10: channelField.optional().describe('CH10: auxiliary'),
      ch11: channelField.optional().describe('CH11: auxiliary'),
      ch12: channelField.optional().describe('CH12: auxiliary'),
      ch13: channelField.optional().describe('CH13: auxiliary'),
      ch14: channelField.optional().describe('CH14: auxiliary'),
      ch15: channelField.optional().describe('CH15: auxiliary'),
      ch16: channelField.optional().describe('CH16: auxiliary'),
      channels: z
        .array(z.number().min(PWM_MIN).max(PWM_MAX))
        .length(16)
        .optional()
        .describe(
          'Alternative: provide all 16 channel values as a single array ' +
          '(overrides individual ch1-ch16 values if provided)'
        ),
      sequence: z
        .number()
        .int()
        .min(0)
        .optional()
        .describe('Optional sequence number for the frame (auto-increments if not set)'),
    },
    annotations: {
      readOnlyHint: false,
      destructiveHint: true,
      idempotentHint: false,
      openWorldHint: true,
    },
  },
  async (params) => {
    try {
      const devClient = ensureClient();
      if (!devClient.isConnected()) {
        return {
          isError: true,
          content: [
            {
              type: 'text' as const,
              text: 'Device not connected. Call `device_connect` first to establish a connection.',
            },
          ],
        };
      }

      // Build channel array
      const channelKeys = [
        'ch1', 'ch2', 'ch3', 'ch4', 'ch5', 'ch6', 'ch7', 'ch8',
        'ch9', 'ch10', 'ch11', 'ch12', 'ch13', 'ch14', 'ch15', 'ch16',
      ];

      let channels: number[];
      if (params.channels) {
        channels = params.channels;
      } else {
        channels = channelKeys.map((key) => {
          const val = (params as Record<string, number | undefined>)[key];
          return val !== undefined ? val : PWM_NEUTRAL;
        });
      }

      const frame = createControlFrame(channels, params.sequence ?? Math.floor(Math.random() * 0xffffffff));
      devClient.sendControl(frame);

      const brief = channels
        .map((v, i) => `CH${i + 1}=${v.toFixed(0)}`)
        .filter((s, i) => channels[i] !== PWM_NEUTRAL)
        .join(', ') || 'all channels at neutral (1500)';

      return {
        content: [
          {
            type: 'text' as const,
            text: `Control frame sent. Non-neutral channels: ${brief}`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to send control: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_stop
// ===========================================================================

server.registerTool(
  'device_stop',
  {
    title: 'Emergency Stop / Neutral',
    description:
      'Send an emergency stop command - sets all 16 channels to neutral ' +
      '(1500us PWM). This stops all motors/servos immediately. Use this ' +
      'when you need to halt the device for safety reasons.',
    inputSchema: {},
    annotations: {
      readOnlyHint: false,
      destructiveHint: true,
      idempotentHint: true,
      openWorldHint: true,
    },
  },
  async () => {
    try {
      const devClient = ensureClient();
      if (!devClient.isConnected()) {
        return {
          isError: true,
          content: [
            {
              type: 'text' as const,
              text: 'Device not connected. Cannot send stop command.',
            },
          ],
        };
      }

      devClient.sendStop();
      return {
        content: [
          {
            type: 'text' as const,
            text: 'STOP sent: All 16 channels set to neutral (1500us). Motors/servos should stop immediately.',
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to send stop: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_get_status
// ===========================================================================

server.registerTool(
  'device_get_status',
  {
    title: 'Get Remote Device System Status',
    description:
      'Retrieve real-time system status from the remote device. Includes ' +
      'CPU usage, memory, network throughput, 4G signal strength, and GPS ' +
      'coordinates (latitude/longitude/altitude/speed/satellites). ' +
      'The remote device broadcasts this data periodically over the DataChannel.',
    inputSchema: {
      format: z
        .enum(['markdown', 'json'])
        .optional()
        .default('markdown')
        .describe('Output format: "markdown" for human-readable, "json" for programmatic use'),
    },
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: true,
    },
  },
  async (params) => {
    try {
      const devClient = ensureClient();
      if (!devClient.isConnected()) {
        return {
          isError: true,
          content: [
            {
              type: 'text' as const,
              text: 'Device not connected. Status is only available when connected to a remote device.',
            },
          ],
        };
      }

      const status = devClient.getLastStatus();

      if (params.format === 'json') {
        return {
          content: [
            {
              type: 'text' as const,
              text: JSON.stringify(status, null, 2),
            },
          ],
        };
      }

      return {
        content: [{ type: 'text' as const, text: formatStatus(status) }],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to get status: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_set_video_config
// ===========================================================================

server.registerTool(
  'device_set_video_config',
  {
    title: 'Configure Video Stream Parameters',
    description:
      'Change the video encoding parameters on the remote device ' +
      '(data_track). This adjusts the FPS and bitrate of the video stream ' +
      'sent over the WebRTC video track. The remote device must have ' +
      'video streaming enabled. Both fps and bitrate are required.',
    inputSchema: {
      fps: z
        .number()
        .int()
        .min(1)
        .max(120)
        .describe('Target frames per second (e.g. 15, 30, 60). Higher values = smoother but more bandwidth.'),
      bitrate: z
        .number()
        .int()
        .min(100000)
        .max(50000000)
        .describe('Target video bitrate in bps (e.g. 1000000 for 1Mbps, 8000000 for 8Mbps).'),
    },
    annotations: {
      readOnlyHint: false,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: true,
    },
  },
  async (params) => {
    try {
      const devClient = ensureClient();
      if (!devClient.isConnected()) {
        return {
          isError: true,
          content: [
            {
              type: 'text' as const,
              text: 'Device not connected. Call `device_connect` first.',
            },
          ],
        };
      }

      devClient.sendVideoConfig(params.fps, params.bitrate);

      return {
        content: [
          {
            type: 'text' as const,
            text: `Video config sent: ${params.fps}fps, ${(params.bitrate / 1e6).toFixed(1)}Mbps. The remote device should apply these settings to its video encoder.`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to set video config: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Tool: device_get_connection_status
// ===========================================================================

server.registerTool(
  'device_get_connection_status',
  {
    title: 'Get Connection Status',
    description:
      'Check the current WebRTC connection state. Returns whether the ' +
      'device is connected, the local client ID, target remote ID, and ' +
      'the underlying connection state machine status.',
    inputSchema: {},
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: true,
    },
  },
  async () => {
    const devClient = client;
    if (!devClient) {
      return {
        content: [
          {
            type: 'text' as const,
            text: 'No device client initialized. Call `device_connect` to establish a connection.',
          },
        ],
      };
    }

    const state = devClient.getState();
    const isConnected = devClient.isConnected();
    const lastStatusTime = devClient.getLastStatusTime();

    return {
      content: [
        {
          type: 'text' as const,
          text: [
            '## Connection Status',
            '',
            `| Property | Value |`,
            `|----------|-------|`,
            `| State | ${state} |`,
            `| Connected | ${isConnected ? 'Yes' : 'No'} |`,
            `| Local ID | ${devClient.getLocalId()} |`,
            `| Remote ID | ${getConfig().remoteId || '(not set)'} |`,
            `| Last Status | ${lastStatusTime > 0 ? new Date(lastStatusTime).toLocaleTimeString() : 'Never'}`,
            '',
          ].join('\n'),
        },
      ],
    };
  }
);

// ===========================================================================
// Tool: device_send_command
// ===========================================================================

server.registerTool(
  'device_send_command',
  {
    title: 'Send Arbitrary JSON Command',
    description:
      'Send an arbitrary JSON command over the DataChannel for extensibility. ' +
      'This allows you to send any command structure that the remote device ' +
      'understands. Use this for custom protocol extensions not covered by ' +
      'the built-in tools. The command is serialized as JSON and sent as ' +
      'binary over the DataChannel.',
    inputSchema: {
      command: z
        .record(z.unknown())
        .describe(
          'JSON command object. Must include a "type" field for the remote ' +
          'device to identify the command. Example: ' +
          '{"type": "custom_action", "param1": "value"}'
        ),
    },
    annotations: {
      readOnlyHint: false,
      destructiveHint: true,
      idempotentHint: false,
      openWorldHint: true,
    },
  },
  async (params) => {
    try {
      const devClient = ensureClient();
      if (!devClient.isConnected()) {
        return {
          isError: true,
          content: [
            {
              type: 'text' as const,
              text: 'Device not connected. Call `device_connect` first.',
            },
          ],
        };
      }

      devClient.sendCommand(params.command as Record<string, unknown>);

      return {
        content: [
          {
            type: 'text' as const,
            text: `Command sent: ${JSON.stringify(params.command)}`,
          },
        ],
      };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return {
        isError: true,
        content: [{ type: 'text' as const, text: `Failed to send command: ${message}` }],
      };
    }
  }
);

// ===========================================================================
// Main: Start transport
// ===========================================================================

async function main() {
  // Attempt auto-connect if remoteId is configured at startup.
  // Whether this succeeds or fails, the MCP server will still start.
  const remoteId = config.remoteId;
  if (remoteId && remoteId.length > 0) {
    console.error(`[mcp] Auto-connecting to remote device "${remoteId}" on startup...`);
    try {
      const devClient = ensureClient();
      await devClient.connect(remoteId);
      console.error(`[mcp] Auto-connect succeeded. Local ID: ${devClient.getLocalId()}`);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      console.error(`[mcp] Auto-connect failed (server will still start): ${msg}`);
    }
  } else {
    console.error('[mcp] No REMOTE_ID configured, skipping auto-connect.');
  }

  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error('[mcp] Remote Control MCP Server started. Waiting for requests...');
}

main().catch((err) => {
  console.error('[mcp] Fatal error:', err);
  process.exit(1);
});
