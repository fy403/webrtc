// SBUS encoder utilities for browser side control.
// Exposes SBUSEncoder and SBUS constants on the window object.
(function (global) {
  const SBUS = {
    START: 0x0f,
    END: 0x00,
    FRAME_SIZE: 25,
    CHANNELS: 16,
    MIN: 172,
    CENTER: 992,
    MAX: 1811,
  };

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function normalizedToValue(n) {
    const normalized = clamp(n, -1, 1);
    if (normalized >= 0) {
      return Math.round(SBUS.CENTER + normalized * (SBUS.MAX - SBUS.CENTER));
    }
    return Math.round(SBUS.CENTER + normalized * (SBUS.CENTER - SBUS.MIN));
  }

  // Encode a full SBUS frame with the first two channels populated.
  function encode({ ch1 = 0, ch2 = 0, ch3 = 0, ch4 = 0, ch5 = 0, ch6 = 0, ch7 = 0, ch8 = 0, ch9, ch10, ch11, ch12, ch13, ch14, ch15, ch16}) {
    const channels = new Uint16Array(SBUS.CHANNELS);
    channels[0] = normalizedToValue(ch1); // forward/backward
    channels[1] = normalizedToValue(ch2); // left/right
    channels[2] = normalizedToValue(ch3);
    channels[3] = normalizedToValue(ch4);
    channels[4] = normalizedToValue(ch5);
    channels[5] = normalizedToValue(ch6);
    channels[6] = normalizedToValue(ch7);
    channels[7] = normalizedToValue(ch8);
    channels[8] = normalizedToValue(ch9);
    channels[9] = normalizedToValue(ch10);
    channels[10] = normalizedToValue(ch11);
    channels[11] = normalizedToValue(ch12);
    channels[12] = normalizedToValue(ch13);
    channels[13] = normalizedToValue(ch14);
    channels[14] = normalizedToValue(ch15);
    channels[15] = normalizedToValue(ch16);

    // 存储当前归一化值用于UI显示
    global.currentChannelValues = [ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8, ch9, ch10, ch11, ch12, ch13, ch14, ch15, ch16];

    const frame = new Uint8Array(SBUS.FRAME_SIZE);
    frame[0] = SBUS.START;

    let bitIndex = 0;
    for (let ch = 0; ch < SBUS.CHANNELS; ch++) {
      let value = channels[ch] || SBUS.CENTER;
      for (let bit = 0; bit < 11; bit++, bitIndex++) {
        const byteIndex = 1 + (bitIndex >> 3);
        const bitPos = bitIndex & 0x07;
        frame[byteIndex] |= ((value >> bit) & 0x01) << bitPos;
      }
    }

    // Flags: no frame-lost or failsafe bits set.
    frame[23] = 0x00;
    frame[24] = SBUS.END;
    return frame;
  }

  global.SBUS = SBUS;
  global.SBUSEncoder = { encode, normalizedToValue, clamp };

  // 全局变量存储当前通道值，用于UI显示
  global.currentChannelValues = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
})(window);

