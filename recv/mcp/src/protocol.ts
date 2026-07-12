// =============================================================================
// RCProtocolV2 - Remote Control Binary Protocol Implementation
// Reference: data_track/include/rc_protocol_v2.h
// =============================================================================

// Protocol constants
const MAGIC1 = 0xaa;
const MAGIC2 = 0x55;
const CONTROL_MSG = 0x01;
const HEARTBEAT_MSG = 0x02;
const FRAME_SIZE = 71; // 2(magic) + 1(type) + 4(seq) + 16*4(channels) = 71 bytes
const CHANNELS = 16;

// Channel value range
const PWM_MIN = 1000;
const PWM_MAX = 2000;
const PWM_NEUTRAL = 1500;

export interface ControlFrame {
  sequence: number;
  channels: Float32Array; // 16 channels, values 1000~2000, neutral=1500
}

/**
 * Encode a control frame to binary buffer (RCProtocolV2)
 * Frame layout:
 *   [0-1]  Magic: 0xAA 0x55
 *   [2]    Type: 0x01 (control)
 *   [3-6]  Sequence: uint32 big-endian
 *   [7-70] Channels: 16 × float32 big-endian
 */
export function encodeControlFrame(frame: ControlFrame): Buffer {
  const buf = Buffer.alloc(FRAME_SIZE);
  let offset = 0;

  // Magic
  buf[offset++] = MAGIC1;
  buf[offset++] = MAGIC2;
  // Type
  buf[offset++] = CONTROL_MSG;
  // Sequence (big-endian)
  buf.writeUInt32BE(frame.sequence >>> 0, offset);
  offset += 4;
  // 16 channels as float32 big-endian
  for (let i = 0; i < CHANNELS; i++) {
    buf.writeFloatBE(frame.channels[i], offset);
    offset += 4;
  }

  return buf;
}

/**
 * Encode a heartbeat frame to binary buffer
 * Same layout as control frame but type=0x02
 */
export function encodeHeartbeatFrame(sequence: number): Buffer {
  const buf = Buffer.alloc(FRAME_SIZE);
  let offset = 0;

  buf[offset++] = MAGIC1;
  buf[offset++] = MAGIC2;
  buf[offset++] = HEARTBEAT_MSG;
  buf.writeUInt32BE(sequence >>> 0, offset);
  offset += 4;
  // Fill remaining channels with neutral
  for (let i = 0; i < CHANNELS; i++) {
    buf.writeFloatBE(PWM_NEUTRAL, offset);
    offset += 4;
  }

  return buf;
}

/**
 * Create a neutral control frame (all channels = 1500)
 */
export function createNeutralFrame(sequence: number): ControlFrame {
  const channels = new Float32Array(CHANNELS);
  channels.fill(PWM_NEUTRAL);
  return { sequence, channels };
}

/**
 * Validate channel values are within valid PWM range
 */
export function validateChannelValue(value: number, channelIndex: number): number {
  if (isNaN(value) || !isFinite(value)) {
    throw new Error(`Channel ${channelIndex + 1}: value must be a finite number, got ${value}`);
  }
  if (value < PWM_MIN || value > PWM_MAX) {
    throw new Error(
      `Channel ${channelIndex + 1}: value ${value} out of range [${PWM_MIN}, ${PWM_MAX}]`
    );
  }
  return value;
}

/**
 * Create control frame from channel values with validation
 */
export function createControlFrame(
  channels: number[] | Float32Array,
  sequence: number
): ControlFrame {
  if (channels.length !== CHANNELS) {
    throw new Error(`Expected ${CHANNELS} channels, got ${channels.length}`);
  }

  const frameChannels = new Float32Array(CHANNELS);
  for (let i = 0; i < CHANNELS; i++) {
    frameChannels[i] = validateChannelValue(channels[i], i);
  }

  return { sequence, channels: frameChannels };
}

/**
 * Encode video config message as JSON string
 * Reference: web/video_rtc.js sendVideoParams()
 */
export function encodeVideoConfigMessage(config: {
  fps?: number;
  bitrate?: number;
  resolution?: string;
  format?: string;
}): Buffer {
  const msg = {
    type: 'video_config',
    resolution: config.resolution || '-1',
    fps: config.fps || 30,
    bitrate: config.bitrate || 8000000,
    format: config.format || '-1',
  };
  return Buffer.from(JSON.stringify(msg), 'utf-8');
}

/**
 * Encode arbitrary command as JSON string (for extensibility)
 */
export function encodeCommandMessage(command: Record<string, unknown>): Buffer {
  return Buffer.from(JSON.stringify(command), 'utf-8');
}

export {
  MAGIC1,
  MAGIC2,
  CONTROL_MSG,
  HEARTBEAT_MSG,
  FRAME_SIZE,
  CHANNELS,
  PWM_MIN,
  PWM_MAX,
  PWM_NEUTRAL,
};
