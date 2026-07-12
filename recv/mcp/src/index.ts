// =============================================================================
// MCP Server Entry Point
// Reference: MCP SDK + mcp_best_practices.md
// =============================================================================

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { SSEServerTransport } from '@modelcontextprotocol/sdk/server/sse.js';
import express from 'express';
import type { Request, Response } from 'express';
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
// Config
// ===========================================================================

loadConfig();
const config = getConfig();
console.error(`[mcp] Config loaded: signaling=${config.signalingUrl}:${config.signalingPort}`);

// ===========================================================================
// Helper: get or create device client (shared across all server instances)
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
// Shared Zod schema
// ===========================================================================

const channelField = z
  .number()
  .min(PWM_MIN)
  .max(PWM_MAX)
  .describe(`PWM value in microseconds (${PWM_MIN}-${PWM_MAX}, neutral=${PWM_NEUTRAL})`);

const durationMsField = z
  .number()
  .int()
  .min(50)
  .max(60000)
  .describe('Duration in milliseconds (50-60000). Control frames are sent continuously at 100ms intervals for this duration, then auto-stop. Like holding a key down.');

// ===========================================================================
// Control State: keep-alive timer management (shared across all McpServer instances)
// ===========================================================================

const CONTROL_INTERVAL_MS = 100; // send control frame every 100ms

let controlInterval: ReturnType<typeof setInterval> | null = null;
let controlTimeout: ReturnType<typeof setTimeout> | null = null;

/**
 * Cancel any running control operation (interval + timeout).
 * Safe to call when no control is active.
 */
function cancelActiveControl(): void {
  if (controlInterval) {
    clearInterval(controlInterval);
    controlInterval = null;
  }
  if (controlTimeout) {
    clearTimeout(controlTimeout);
    controlTimeout = null;
  }
}

/**
 * Send a neutral (stop) frame to the device if connected.
 */
function sendStopToDevice(): void {
  if (!client || !client.isConnected()) return;
  try {
    client.sendStop();
  } catch (_) { /* ignore */ }
}

// ===========================================================================
// Factory: create a new McpServer with all tools registered
// Each SSE connection gets its own server instance because McpServer
// can only be bound to ONE transport at a time.
// ===========================================================================

function createMcpServer(): McpServer {
  const server = new McpServer({
    name: 'remote-control-mcp-server',
    version: '1.0.0',
  });

  // -----------------------------------------------------------------------
  // Tool: device_connect
  // -----------------------------------------------------------------------

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

        await devClient.connect(params.remoteId);

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

  // -----------------------------------------------------------------------
  // Tool: device_disconnect
  // -----------------------------------------------------------------------

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
        cancelActiveControl();
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

  // -----------------------------------------------------------------------
  // Tool: device_send_control
  // -----------------------------------------------------------------------

  server.registerTool(
    'device_send_control',
    {
      title: 'Send 16-Channel Control Frame (Continuous)',
      description:
        'Send RC control frames continuously for a specified duration, ' +
        'simulating holding a joystick/key down. Frames are sent every ' +
        '100ms to keep the device active, and auto-stop when the duration ' +
        'expires. If another control command is issued during execution, ' +
        'the previous one is cancelled.\n\n' +
        'Channel assignments:\n' +
        '- CH1: Throttle (1000=reverse, 1500=stop, 2000=forward)\n' +
        '- CH2: Steering (1000=left, 1500=center, 2000=right)\n' +
        '- CH3-CH16: Auxiliary channels\n\n' +
        'This tool blocks until the duration completes, then returns.',
      inputSchema: {
        durationMs: durationMsField,
        ch1: channelField.optional().describe('CH1: throttle (1000=reverse, 1500=stop, 2000=forward)'),
        ch2: channelField.optional().describe('CH2: steering (1000=left, 1500=center, 2000=right)'),
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

        // Cancel any previous control operation
        cancelActiveControl();

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

        const durationMs = params.durationMs;
        const nonNeutral = channels
          .map((v, i) => `CH${i + 1}=${v.toFixed(0)}`)
          .filter((_s, i) => channels[i] !== PWM_NEUTRAL)
          .join(', ') || 'all channels at neutral (1500)';

        console.error(`[control] Starting continuous control: ${nonNeutral}, duration=${durationMs}ms`);

        let seq = 0;

        // Send first frame immediately
        if (devClient.isConnected()) {
          try {
            devClient.sendControl(createControlFrame(channels, seq++));
          } catch (_) { /* ignore */ }
        }

        // Start interval: send every 100ms
        controlInterval = setInterval(() => {
          if (!devClient.isConnected()) {
            cancelActiveControl();
            return;
          }
          try {
            devClient.sendControl(createControlFrame(channels, seq++));
          } catch (_) {
            cancelActiveControl();
          }
        }, CONTROL_INTERVAL_MS);

        // Wait for duration, then auto-stop
        const stopped = await new Promise<boolean>((resolve) => {
          controlTimeout = setTimeout(() => {
            cancelActiveControl();
            sendStopToDevice();
            resolve(true); // normal completion
          }, durationMs);
        });

        return {
          content: [
            {
              type: 'text' as const,
              text: stopped
                ? `Control executed: ${nonNeutral} for ${durationMs}ms. Auto-stopped.`
                : `Control interrupted. Channels returned to neutral.`,
            },
          ],
        };
      } catch (err: unknown) {
        cancelActiveControl();
        sendStopToDevice();
        const message = err instanceof Error ? err.message : String(err);
        return {
          isError: true,
          content: [{ type: 'text' as const, text: `Failed to send control: ${message}` }],
        };
      }
    }
  );

  // -----------------------------------------------------------------------
  // Tool: device_stop
  // -----------------------------------------------------------------------

  server.registerTool(
    'device_stop',
    {
      title: 'Emergency Stop / Neutral',
      description:
        'Immediately stop all motors/servos by setting all 16 channels to ' +
        'neutral (1500us PWM). Also cancels any running continuous control ' +
        'operation from `device_send_control`. Use for emergency halt.',
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
        // Cancel any running continuous control
        const wasActive = controlInterval !== null;
        cancelActiveControl();

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
              text: wasActive
                ? 'STOP: Continuous control cancelled + all channels set to neutral (1500us).'
                : 'STOP sent: All 16 channels set to neutral (1500us). Motors/servos should stop immediately.',
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

  // -----------------------------------------------------------------------
  // Tool: device_get_status
  // -----------------------------------------------------------------------

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

  // -----------------------------------------------------------------------
  // Tool: device_set_video_config
  // -----------------------------------------------------------------------

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

  // -----------------------------------------------------------------------
  // Tool: device_get_connection_status
  // -----------------------------------------------------------------------

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

  // -----------------------------------------------------------------------
  // Tool: device_send_command
  // -----------------------------------------------------------------------

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

  return server;
}

// ===========================================================================
// Main: Start transport
// ===========================================================================

async function main() {
  const transportMode = config.transportMode;

  if (transportMode === 'sse') {
    // ---- SSE Transport Mode ----
    // Each SSE connection gets its OWN McpServer instance,
    // because McpServer can only be bound to ONE transport.
    await startSSEServer();
  } else {
    // ---- Stdio Transport Mode (default) ----
    // Single server, single transport - this is fine for stdio.
    const server = createMcpServer();
    const transport = new StdioServerTransport();
    await server.connect(transport);
    console.error('[mcp] Remote Control MCP Server started (stdio). Waiting for requests...');
  }

  // Attempt auto-connect in background AFTER transport is up.
  const remoteId = config.remoteId;
  if (remoteId && remoteId.length > 0) {
    console.error(`[mcp] Auto-connecting to remote device "${remoteId}" on startup...`);
    try {
      const devClient = ensureClient();
      await devClient.connect(remoteId);
      console.error(`[mcp] Auto-connect succeeded. Local ID: ${devClient.getLocalId()}`);
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      console.error(`[mcp] Auto-connect failed (server is running, use device_connect to retry): ${msg}`);
    }
  } else {
    console.error('[mcp] No REMOTE_ID configured, skipping auto-connect.');
  }
}

// --------------------------------------------------------------------------
// SSE Server
// --------------------------------------------------------------------------

type SessionEntry = {
  transport: SSEServerTransport;
  mcpServer: McpServer;
};

async function startSSEServer(): Promise<void> {
  const app = express();
  app.use(express.json());

  const sessions = new Map<string, SessionEntry>();

  // Health check endpoint
  app.get('/health', (_req: Request, res: Response) => {
    res.json({ status: 'ok', transport: 'sse', sessions: sessions.size });
  });

  // GET /sse - Establish SSE stream for client
  // Each connection gets its OWN McpServer instance with all tools registered.
  app.get('/sse', async (req: Request, res: Response) => {
    console.error('[sse] New SSE connection established');

    // Create a fresh McpServer for this connection
    const mcpServer = createMcpServer();
    const transport = new SSEServerTransport('/messages', res);
    sessions.set(transport.sessionId, { transport, mcpServer });

    res.on('close', () => {
      console.error(`[sse] SSE connection closed: ${transport.sessionId}`);
      sessions.delete(transport.sessionId);
      // McpServer will be garbage-collected since nothing else references it
    });

    await mcpServer.connect(transport);
  });

  // POST /messages - Receive client messages
  app.post('/messages', async (req: Request, res: Response) => {
    const sessionId = req.query.sessionId as string;
    if (!sessionId) {
      res.status(400).json({ error: 'Missing sessionId query parameter' });
      return;
    }

    const session = sessions.get(sessionId);
    if (!session) {
      res.status(400).json({ error: `No active SSE transport for sessionId: ${sessionId}` });
      return;
    }

    await session.transport.handlePostMessage(req, res, req.body);
  });

  const port = config.ssePort;
  const host = config.sseHost;

  app.listen(port, host, () => {
    console.error(`[mcp] Remote Control MCP Server started (SSE) on http://${host}:${port}`);
    console.error(`[mcp] SSE endpoint: GET  http://${host}:${port}/sse`);
    console.error(`[mcp] Messages:     POST http://${host}:${port}/messages?sessionId=...`);
  });
}

main().catch((err) => {
  console.error('[mcp] Fatal error:', err);
  process.exit(1);
});
