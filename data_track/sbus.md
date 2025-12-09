# SBUS 协议说明文档

## 1. 什么是 SBUS 协议？

SBUS（Serial Bus）是由 Futaba 公司开发的一种数字串行通信协议，主要用于遥控器系统中接收机与飞控、舵机控制器等设备之间的通信。相比传统的 PWM 信号，SBUS 具有以下优势：

- 单线串行传输：只需要一根信号线即可传输多达 16 个通道的数据
- 高更新频率：每秒可传输约 70 帧数据（约 14ms/帧）
- 数字化精度：每个通道具有 11 位分辨率（值范围为 0-2047）
- 抗干扰能力强：采用串行传输和校验机制，比模拟 PWM 更稳定

## 2. SBUS 数据格式

SBUS 帧总长度为 25 字节，具体结构如下：

| 字节偏移 | 内容             | 大小 | 说明 |
|----------|------------------|------|------|
| 0        | 起始字节         | 1 字节 | 固定值 0x0F |
| 1-22     | 通道数据         | 22 字节 | 16 个通道，每个通道 11 位 |
| 23       | 标志位           | 1 字节 | 包含帧丢失、故障保护等状态 |
| 24       | 结束字节         | 1 字节 | 固定值 0x00 |

### 2.1 通道数据布局

16 个通道的数据被打包在 22 字节中，每个通道占用 11 位。通道数值的有效范围是：
- 最小值：172
- 中间值：992
- 最大值：1811

通道值转换公式：
- 当值 ≥ 992 时：归一化值 = (值 - 992) / (1811 - 992)
- 当值 < 992 时：归一化值 = (值 - 992) / (992 - 172)

### 2.2 标志位字节（第 23 字节）

标志位字节包含以下信息：
- Bit 0-1：未使用
- Bit 2：帧丢失标志（1 表示帧丢失）
- Bit 3：故障保护激活标志（1 表示故障保护激活）
- Bit 4-7：未使用

## 3. 本项目中的 SBUS 实现

### 3.1 JavaScript 端实现

在浏览器端，通过 [web/sbus_encoder.js](file:///C:/Users/Administrator/tools/RTSP/remote_control/webrtc/data_track/web/sbus_encoder.js) 文件实现了 SBUS 编码功能：

1. 提供了 `normalizedToValue()` 函数用于将 [-1, 1] 归一化值转换为 SBUS 通道值
2. 使用 `encode()` 函数生成完整的 SBUS 帧，主要控制前两个通道：
   - 通道 0（CH1）：前进/后退控制
   - 通道 1（CH2）：左转/右转控制
3. 所有其他通道默认设置为中间值 992

### 3.2 C++ 端实现

在 C++ 端，通过 [src/message_handler.cpp](file:///C:/Users/Administrator/tools/RTSP/remote_control/webrtc/data_track/src/message_handler.cpp) 和 [include/message_handler.h](file:///C:/Users/Administrator/tools/RTSP/remote_control/webrtc/data_track/include/message_handler.h) 实现了 SBUS 解析功能：

1. `parseSbusFrame()` 函数负责解析接收到的 SBUS 帧
2. 从帧中提取 16 个通道的数据，并检查帧状态（帧丢失、故障保护）
3. `sbusToNormalized()` 函数将 SBUS 通道值转换为 [-1, 1] 的归一化值

### 3.3 工作流程

1. 浏览器端用户界面捕获控制输入（如虚拟摇杆）
2. 将控制输入转换为 [-1, 1] 归一化值
3. 使用 JavaScript 的 SBUS 编码器生成完整 SBUS 帧
4. 通过 WebRTC DataChannel 发送 SBUS 帧到远程设备
5. 远程设备接收并解析 SBUS 帧
6. 提取通道数据并转换为实际控制信号驱动电机

这种方式使得我们能够通过现代 Web 技术实现对遥控设备的精确控制，同时保持了与传统 SBUS 生态系统的兼容性。