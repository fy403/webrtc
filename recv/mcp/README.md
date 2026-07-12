# Remote Control MCP Server

MCP (Model Context Protocol) server for remote device control via WebRTC DataChannel.

## Architecture

```
┌──────────────────────┐      stdio (JSON-RPC)      ┌──────────────────────┐
│   AI / MCP Client    │ ◄──────────────────────► │  MCP Server (Node)   │
│  (Claude Desktop)    │                           │                      │
└──────────────────────┘                           └──────────┬───────────┘
                                                              │
                                                 WebSocket    │  WebRTC DataChannel
                                                 Signaling    │  (RCProtocolV2 Binary)
                                                              │
                                                     ┌────────▼───────────┐
                                                     │   Remote Device    │
                                                     │   (data_track)     │
                                                     │                    │
                                                     │  CPU/Mem/Servo/    │
                                                     │  GPS/4G/Video      │
                                                     └────────────────────┘
```

## Protocol Stack

| Layer | Format | Direction |
|-------|--------|-----------|
| **Control** (SBUS/PWM) | RCProtocolV2 binary, 71 bytes, 16ch × float32 | MCP → Device |
| **Heartbeat** | RCProtocolV2 binary, type=0x02 | MCP → Device |
| **System Status** | JSON text (CPU, MEM, NET, GPS, 4G) | MCP ← Device |
| **Video Config** | JSON `{type:"video_config", fps, bitrate}` | MCP → Device |
| **Custom Command** | JSON (任意) | MCP → Device |
| **Signaling** | JSON `{id, type, description/candidate}` | MCP ↔ Signaling Server |

## Requirements

- **Node.js** >= 18.0.0
- **Linux** (Ubuntu/Debian) or **Windows**
- **libdatachannel** native dependencies (Linux: `libssl-dev`, `libsrtp2-dev`, `libusrsctp-dev`, `libjuice-dev`)
- Access to the signaling server (default: `119.45.178.251:8000`)
- Remote device running `data_track` with a matching `client_id`

## Quick Start

```bash
# 1. Install dependencies & build
cd mcp
chmod +x install.sh run.sh
./install.sh

# 2. Edit config (set your remote device ID)
# Edit config.json and set "remoteId": "data_Dd8fgkoKo90"

# 3. Run directly for testing
REMOTE_ID=data_Dd8fgkoKo90 node dist/index.js
```

## MCP Tools

### 1. `device_connect`

Establish WebRTC DataChannel connection to a remote device.

```
Parameters:
  remoteId (string, required)       - Remote device client_id
  autoReconnect (boolean, optional) - Default: true
```

### 2. `device_disconnect`

Cleanly disconnect from the remote device.

```
Parameters: none
```

### 3. `device_send_control`

Send a 16-channel RC control frame (RCProtocolV2 binary format).

```
Parameters:
  ch1..ch16 (number, optional) - PWM values 1000~2000 (neutral=1500)
  
  Or use array form:
  channels (number[16], optional) - All 16 channels as array

Channel Assignments (typical):
  CH1 - Throttle/Forward  (1000=reverse, 1500=stop, 2000=forward)
  CH2 - Steering/Turn     (1000=left, 1500=center, 2000=right)
  CH3..CH16 - Auxiliary   (switches, gimbals, custom functions)
```

### 4. `device_stop`

Emergency stop - sets all 16 channels to neutral (1500us).

```
Parameters: none
```

### 5. `device_get_status`

Retrieve real-time system telemetry from the remote device.

```
Parameters:
  format (enum, optional) - "markdown" (default) or "json"

Returns:
  - CPU usage, per-core usage, load averages
  - Memory: total/used/free/percentage
  - Network: RX/TX throughput (Kbps)
  - 4G: signal strength, SIM status, network type
  - GPS: latitude, longitude, altitude, speed, satellites
```

### 6. `device_set_video_config`

Change video encoding parameters on the remote device.

```
Parameters:
  fps (integer, required)    - Target FPS (1-120)
  bitrate (integer, required) - Target bitrate in bps (100000~50000000)
                                e.g. 8000000 for 8Mbps
```

### 7. `device_get_connection_status`

Check current WebRTC connection state.

```
Parameters: none

Returns:
  state, connected, localId, remoteId, lastStatusTime
```

### 8. `device_send_command`

Send arbitrary JSON command for protocol extension.

```
Parameters:
  command (object, required) - Any JSON object with "type" field
```

## Configuration

### config.json

```json
{
  "signalingUrl": "119.45.178.251",
  "signalingPort": 8000,
  "stunServer": "stun.l.google.com",
  "stunPort": 19302,
  "turnServer": "119.45.178.251",
  "turnPort": 3478,
  "turnUser": "fy403",
  "turnPass": "qwertyuiop",
  "remoteId": "",
  "autoReconnect": true,
  "reconnectIntervalMs": 3000,
  "controlTimeoutMs": 500,
  "heartbeatIntervalMs": 300
}
```

### Environment Variables

Override config values via environment:

```
REMOTE_ID             - Remote device ID
SIGNALING_URL         - Signaling server host
SIGNALING_PORT        - Signaling server port
STUN_SERVER           - STUN server host
TURN_SERVER           - TURN server host
TURN_USER             - TURN username
TURN_PASS             - TURN password
```

## Claude Desktop Integration

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "remote-control": {
      "command": "node",
      "args": ["/path/to/webrtc/mcp/dist/index.js"],
      "env": {
        "REMOTE_ID": "data_Dd8fgkoKo90",
        "SIGNALING_URL": "119.45.178.251"
      }
    }
  }
}
```

## Example Usage via MCP

```
1. device_connect { "remoteId": "data_Dd8fgkoKo90" }
   → Connected to remote device

2. device_get_status { "format": "markdown" }
   → Shows CPU 35%, Memory 512MB/2GB, GPS 39.9042°N 116.4074°E...

3. device_send_control { "ch1": 1600, "ch2": 1500 }
   → Sends throttle forward at 20%, steering centered

4. device_set_video_config { "fps": 30, "bitrate": 4000000 }
   → Configures video to 30fps @ 4Mbps

5. device_stop {}
   → Emergency stop - all channels to neutral
```

## Protocol Reference

### RCProtocolV2 Binary Frame (71 bytes)

```
Offset  Size  Field       Description
------  ----  -----       -----------
0       2     Magic       0xAA 0x55
2       1     Type        0x01 = Control, 0x02 = Heartbeat
3       4     Sequence    uint32 big-endian
7       64    Channels    16 × float32 big-endian (1000~2000us)
```

### Signaling Messages (JSON over WebSocket)

```json
// Offer/Answer
{"id":"peer_id","type":"offer","description":"v=0\r\n..."}
{"id":"peer_id","type":"answer","description":"v=0\r\n..."}

// ICE Candidate
{"id":"peer_id","type":"candidate","candidate":"candidate:...","mid":"0"}

// Connection Close
{"id":"peer_id","type":"peer_close"}
```

### System Status (JSON over DataChannel)

```json
{
  "connection_count": "1",
  "rx_speed": "150",
  "tx_speed": "80",
  "cpu_usage": "3500",
  "cpu_core_count": "4",
  "mem_total_mb": "2048",
  "mem_used_mb": "512",
  "gps_latitude": "39.9042",
  "gps_longitude": "116.4074",
  "gps_satellites": "12",
  "gps_speed_kmh": "15.5"
}
```

## Extending

### Adding New Tools

Add tool registration in `src/index.ts`:

```typescript
server.registerTool(
  'device_custom_action',
  {
    title: 'Custom Action',
    description: 'Description of the custom action',
    inputSchema: { ... },
    annotations: { ... },
  },
  async (params) => {
    const devClient = ensureClient();
    // ... implement action
    return { content: [{ type: 'text', text: 'Result' }] };
  }
);
```

### Adding New Protocol Commands

Add encoder in `src/protocol.ts`, then use via `device_send_command` or a new tool.

## Building from Source

```bash
npm install
npm run build
# Output in dist/
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `node-datachannel` build fails | Install native deps: `sudo apt install libssl-dev libsrtp2-dev libusrsctp-dev libjuice-dev` |
| Connection timeout | Verify remote device is online and signaling server is reachable |
| No status data | The remote device sends status every 1s. Wait a moment after connecting. |
| Control has no effect | Check channel mapping - different motor drivers use different channel layouts |
| `Cannot find module` errors | Run `npm install && npm run build` |
