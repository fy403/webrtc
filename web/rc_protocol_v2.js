// RC Protocol v2 - 完全替代SBUS的新协议
// 直接传输-1.0~1.0浮点数，无需转换

(function(global) {
  'use strict';

  // 协议常量
  const PROTOCOL = {
    MAGIC1: 0xAA,
    MAGIC2: 0x55,
    CONTROL_MSG: 0x01,
    HEARTBEAT_MSG: 0x02,  // 心跳包类型
    FRAME_SIZE: 67  // 固定帧大小: 2 + 1 + 16*4 = 67字节
  };

  /**
   * 编码控制消息
   * @param {Object} channels - 通道值 {ch1: -1.0~1.0, ch2: -1.0~1.0, ...}
   * @returns {Uint8Array} - 编码后的数据包（67字节）
   */
  function encode(channels) {
    return encodeFrame(channels, PROTOCOL.CONTROL_MSG);
  }

  /**
   * 编码心跳包
   * 心跳包只更新心跳时间，不调用电机控制
   * @returns {Uint8Array} - 编码后的心跳包（67字节）
   */
  function encodeHeartbeat() {
    return encodeFrame({}, PROTOCOL.HEARTBEAT_MSG);
  }

  /**
   * 编码通用帧（内部函数）
   * @param {Object} channels - 通道值
   * @param {number} msgType - 消息类型（CONTROL_MSG或HEARTBEAT_MSG）
   * @returns {Uint8Array} - 编码后的数据包
   */
  function encodeFrame(channels, msgType) {
    // 使用ArrayBuffer和DataView来正确处理float32
    const buffer = new ArrayBuffer(PROTOCOL.FRAME_SIZE);
    const view = new DataView(buffer);

    // 帧头
    view.setUint8(0, PROTOCOL.MAGIC1);
    view.setUint8(1, PROTOCOL.MAGIC2);
    view.setUint8(2, msgType);

    // 16个通道，每个4字节float32（大端序）
    for (let i = 0; i < 16; i++) {
      const value = channels['ch' + (i + 1)] || 0;
      const clampedValue = Math.max(-1.0, Math.min(1.0, value));
      view.setFloat32(3 + i * 4, clampedValue, false); // big-endian
    }

    // 存储当前通道值用于UI显示（仅控制包）
    if (msgType === PROTOCOL.CONTROL_MSG) {
      global.currentChannelValues = [];
      for (let i = 0; i < 16; i++) {
        const value = channels['ch' + (i + 1)] || 0;
        global.currentChannelValues.push(value);
      }
    }

    return new Uint8Array(buffer);
  }

  // 导出
  global.RCProtocol = { encode, encodeHeartbeat, PROTOCOL };
  global.currentChannelValues = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

})(window);
