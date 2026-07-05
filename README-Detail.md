# 详细文档 · WebRTC 远程遥控车系统 V2

> 本文档是 [README.md](README.md) 的详细补充，包含完整的部署步骤、硬件选型、技术细节与 Q&A。

---

## 目录

- [详细文档 · WebRTC 远程遥控车系统 V2](#详细文档--webrtc-远程遥控车系统-v2)
  - [目录](#目录)
  - [系统架构](#系统架构)
    - [V2 版本重大变化](#v2-版本重大变化)
    - [连接建立流程](#连接建立流程)
    - [RC Protocol V2](#rc-protocol-v2)
  - [快速开始](#快速开始)
    - [前置依赖说明](#前置依赖说明)
    - [Step 1：部署信令服务器](#step-1部署信令服务器)
    - [Step 2：配置 STUN / TURN 服务器](#step-2配置-stun--turn-服务器)
    - [Step 3：安装开发板依赖](#step-3安装开发板依赖)
      - [Rockchip MPP 硬件编码（可选）](#rockchip-mpp-硬件编码可选)
    - [Step 4：查找设备参数](#step-4查找设备参数)
      - [4.1 摄像头参数获取](#41-摄像头参数获取)
      - [4.2 麦克风参数获取](#42-麦克风参数获取)
      - [4.3 扬声器参数获取](#43-扬声器参数获取)
      - [4.4 电机驱动器接口获取](#44-电机驱动器接口获取)
      - [4.5 4G 网络模块接口获取](#45-4g-网络模块接口获取)
    - [Step 5：编译 \& 配置](#step-5编译--配置)
      - [5.1 编译程序](#51-编译程序)
      - [5.2 配置音视频采集端（av\_track）](#52-配置音视频采集端av_track)
      - [5.3 配置数据控制端（data\_track）](#53-配置数据控制端data_track)
      - [5.4 配置命令终端（cmd\_track）](#54-配置命令终端cmd_track)
      - [5.5 快速重置 CLIENT\_ID](#55-快速重置-client_id)
      - [5.6 无摄像头调试模式（fake camera）](#56-无摄像头调试模式fake-camera)
    - [Step 6：启动服务](#step-6启动服务)
    - [Step 7：打开控制端网页](#step-7打开控制端网页)
  - [材料清单](#材料清单)
  - [硬件选型指南](#硬件选型指南)
    - [开发板](#开发板)
    - [电机驱动方案](#电机驱动方案)
    - [网络模块](#网络模块)
    - [摄像头模块](#摄像头模块)
    - [声音模块](#声音模块)
  - [项目 Q\&A](#项目-qa)
    - [Q1：如何添加自定义的电机驱动方案？](#q1如何添加自定义的电机驱动方案)
    - [Q2：必须购买所有配件吗？](#q2必须购买所有配件吗)
    - [Q3：图传延时能达到多少？](#q3图传延时能达到多少)
    - [Q4：V2 协议与 V1 如何兼容？](#q4v2-协议与-v1-如何兼容)
    - [Q5：cmd\_track 远程终端有什么用？](#q5cmd_track-远程终端有什么用)
  - [后续计划](#后续计划)

---

## 系统架构

<img src="README.assets/image-20251030170418827.png" alt="系统架构图" width="85%" />

整套系统由四个核心部分组成：

**1. 信令服务器（Signaling Server）**  
负责 WebRTC 握手阶段的 SDP 与 ICE 候选地址交换，使用 WebSocket 通信。支持 Node.js 和 Python3 两种实现，带宽需求极低（70元/年的 2H2G 服务器即可支撑）。

**2. 音视频采集端（`av_track`）**  
运行在开发板上，通过 FFmpeg 采集摄像头（USB / MIPI CSI）与麦克风数据，编码为 H.264/H.265 + Opus，经 libdatachannel 推送至浏览器。V2 支持 Rockchip MPP 硬件编码加速。

**3. 数据控制端（`data_track`）**  
接收浏览器的控制帧（RC Protocol V2，16通道），解析后通过串口驱动电机控制板（支持 CRSF-PWM / 原生 GPIO PWM 两种方案）。同时采集 GPS、陀螺仪、电量、CPU/内存等遥测数据，通过 DataChannel 回传至控制端展示。

**4. 命令终端（`cmd_track`）**  
通过独立 WebRTC DataChannel 隧道传输 Shell 命令与输出。浏览器端使用 xterm.js 渲染远程终端，支持 Ctrl+C 信号、`av_track`/`data_track` 容器重启等运维操作。

### V2 版本重大变化

相比 V1 版本，V2 进行了以下重大升级：

| 功能 | V1 | V2 |
|------|-----|-----|
| 控制通道数 | 2 通道（前后/左右） | 16 通道 raw PWM（1000~2000μs） |
| 控制协议 | 简单百分比映射 | RC Protocol V2，71字节固定帧，序列号+心跳 |
| 电机驱动 | UART / CRSF | CRSF-PWM / 原生 GPIO PWM |
| 控制器 | 仅键盘 | 键盘 / Xbox 手柄 / 虚拟摇杆 / 手机陀螺仪 |
| 速度曲线 | 无 | 可视化曲线编辑器，每通道独立配置 |
| 视频编码 | 软编码 H.264/H.265 | 软编码 + Rockchip MPP 硬编码（h264_rkmpp / hevc_rkmpp） |
| 摄像头 | USB 摄像头 | USB + MIPI CSI（OV5647 / IMX219 配置工具） |
| 远程运维 | 无 | cmd_track 远程 Shell 终端 |
| 前端面板 | 基础连接状态 | Video/Data/CMD 三路独立面板 + 16通道柱状图 + 速度仪表盘 + CPU/MEM |

### 连接建立流程

```
控制端(浏览器)               信令服务器              开发板(被控端)
     │                          │                        │
     │─── WebSocket ───────────►│◄── WebSocket ──────────│
     │                          │                        │
     │◄────────── SDP Offer / Answer 交换 ───────────────►│
     │◄────────── ICE Candidate 交换 ────────────────────►│
     │                          │                        │
     │◄═════ VIDEO LINK (av_track, RTP 音视频) ═══════════►│
     │◄═════ DATA LINK  (data_track, DataChannel) ════════►│
     │◄═════ CMD LINK   (cmd_track, DataChannel) ═════════►│
```

WebRTC ICE 优先建立 P2P 直连；若 NAT 穿透失败，自动回退至 TURN 中继，保障远端连通性。

### RC Protocol V2

V2 协议完全替代了 V1 的简单百分比映射，采用固定帧格式传输16个通道的 raw PWM 值：

| 字段 | 大小 | 说明 |
|------|------|------|
| 帧头 Magic | 2 bytes | `0xAA 0x55` |
| 消息类型 | 1 byte | `0x01`=控制帧，`0x02`=心跳帧 |
| 序列号 | 4 bytes | 递增序列号（心跳帧为0） |
| 通道数据 | 64 bytes | 16通道 × 4字节 float32（大端序） |
| **总大小** | **71 bytes** | 固定帧大小 |

每个通道值范围 1000~2000，中位 1500，直接对应 PWM 脉宽（微秒），无需额外换算。

---

## 快速开始

### 前置依赖说明

| 组件 | 用途 | 备注 |
|------|------|------|
| 信令服务器 | SDP/ICE 交换 | 调试期可直接使用作者公共服务 |
| STUN 服务器 | 获取公网地址 | 使用 `stun.l.google.com:19302` 即可 |
| TURN 服务器 | NAT 穿透失败时中继 | 可自建或使用 Cloudflare 免费节点 |
| 开发板 | 运行采集/控制程序 | 任意支持 Linux + FFmpeg + C++ 的 ARM 板 |
| 摄像头 | 视频采集 | USB 免驱摄像头 或 MIPI CSI 摄像头 |

---

### Step 1：部署信令服务器

> 💡 **跳过提示**：如果只是本地调试，可直接使用作者公共信令服务器，但必须为你的设备配置唯一的 `CLIENT_ID`，避免冲突。

```shell
git clone https://github.com/fy403/webrtc
```

**方式一：Node.js**

```shell
cd webrtc/signaling_server/nodejs
sudo apt install nodejs npm
npm install
chmod +x ./install && ./install
```

**方式二：Python3**

```shell
cd webrtc/signaling_server/python3
sudo apt install python3 python3-pip
pip3 install -r requirements.txt
chmod +x ./install && ./install
```

> 默认监听端口 **8000**，记得在服务器防火墙放行 `TCP:8000`。

---

### Step 2：配置 STUN / TURN 服务器

公共 STUN 直接使用即可：

```
stun.l.google.com:19302
```

TURN 服务器可选方案：
- **自建**：参考 [搭建私有 TURN 服务器](turn_server/README.md)
- **免费**：使用 [Cloudflare TURN](https://pidan.dev/20250722/webrtc-livekit-deploy-config-turn-server/)（延迟略高）

---

### Step 3：安装开发板依赖

> 💡 **推荐使用 Docker 镜像**，可跳过以下繁琐步骤，直接进入 [Step 6：启动服务](#step-6启动服务)。

**手动安装依赖（以 Ubuntu/ARM 为例）：**

```shell
# 基础工具链
sudo apt install -y g++ make dos2unix git libsdl2-dev libssl-dev
sudo apt-get install -y nlohmann-json3-dev

# FFmpeg（推荐 4.4.2 版本）
sudo apt-get install -y libavdevice-dev libavformat-dev libavcodec-dev \
    libavutil-dev libswscale-dev x264 libx264-dev ffmpeg
# 若版本不兼容报错，建议手动编译 ffmpeg==4.4.2
# 或将报错提交给 AI Agent 进行适配
```

```shell
# CMake 3.28.3（手动编译）
wget https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3.tar.gz
tar -xzvf cmake-3.28.3.tar.gz && cd cmake-3.28.3
./configure && make -j3 && sudo make install
ln -sf /usr/local/bin/cmake /usr/bin/cmake
```

```shell
# libdatachannel（WebRTC 数据通道库）
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel && git submodule update --init --recursive
mkdir build && cd build && cmake .. && make -j2 && sudo make install
```

> 💡 ARM 开发板建议换用清华 apt 镜像源加速下载。

#### Rockchip MPP 硬件编码（可选）

如使用 Rockchip 芯片（如 RK3588、RK3568 等），可安装 MPP 硬件编码支持以大幅降低 CPU 占用：

```shell
# 确认芯片支持
ls /dev/mpp_service

# 编译安装 MPP
git clone https://github.com/rockchip-linux/mpp.git
cd mpp/build/linux/aarch64
cmake ../.. && make -j4 && sudo make install

# 确认 H.264 硬编码可用
ffmpeg -encoders 2>&1 | grep rkmpp
# 输出应包含: h264_rkmpp, hevc_rkmpp
```

---

### Step 4：查找设备参数

在运行程序前，需要确认各硬件设备的标识符。

#### 4.1 摄像头参数获取

**USB 摄像头：**

```shell
# 列出所有视频设备
sudo v4l2-ctl --list-device

# 查看摄像头支持的格式/分辨率/帧率
sudo v4l2-ctl -d /dev/video0 --list-formats-ext
```

**MIPI CSI 摄像头：**

V2 提供了 `camera/` 目录下的配置工具，支持直接配置传感器分辨率与帧率。

支持的传感器类型：`ov5647`、`imx219`

```shell
# OV5647 配置示例
cd camera/ov5647-config
sudo ./install.sh

# 列出支持的分辨率和帧率
sudo ov5647-config-tool.sh -l

# 配置为 1080p @ 30fps
sudo ov5647-config-tool.sh -r 1920x1080

# 配置为 720p @ 60fps
sudo ov5647-config-tool.sh -r 1280x720
```

> 💡 使用 MIPI CSI 摄像头时，`av_track/config.txt` 中 `videoType` 设为对应传感器类型。

#### 4.2 麦克风参数获取

```shell
arecord -L
# 示例输出: hw:CARD=Audio,DEV=0  （第一个 USB 麦克风）

arecord --device=hw:CARD=Audio,DEV=0 --dump-hw-params
# 查看: FORMAT=S16_LE, CHANNELS=1, RATE=48000
```

#### 4.3 扬声器参数获取

```shell
aplay -l
cat /proc/asound/card3/stream0
# 查看 Playback: Format, Channels, Rates
```

#### 4.4 电机驱动器接口获取

```shell
# CRSF-PWM 转换器（通过串口）
ls /dev/ttyS*
# 或 USB 串口
ls /dev/ttyUSB*

# 原生 GPIO PWM（需要芯片支持 PWM 控制器）
ls /sys/class/pwm/
```

#### 4.5 4G 网络模块接口获取

使用 RNDIS 的 4G 模块，通常第一个以 `enx` 开头的即为 4G 网卡。

```shell
ip a
# 5: enx2089846a96ab: ...
#     inet 192.168.10.2/24 ...

ip route
# default via 192.168.10.1 dev enx2089846a96ab ...
```

> 如果没有 4G 模块，直接让开发板与电脑连接同一 WiFi 也可以控制。

---

### Step 5：编译 & 配置

#### 5.1 编译程序

```shell
git clone https://github.com/fy403/webrtc
cd webrtc

# 一键全量编译
./build-all.sh

# 或分模块编译
cd av_track   && chmod +x build.sh install.sh && ./build.sh
cd data_track && chmod +x build.sh install.sh && ./build.sh
cd cmd_track  && chmod +x build.sh install.sh && ./build.sh
```

#### 5.2 配置音视频采集端（av_track）

编辑 `av_track/config.txt` 配置文件（使用 `key=value` 格式，支持 `#` 注释）：

```ini
# =============================================================================
# Signaling Server Configuration (信令服务器配置)
# =============================================================================
webSocketServer=fy403.cn          # 信令服务器地址
webSocketPort=8000                # 信令服务器端口
client_id=usbcam                  # ⚠️ 必须全局唯一

# =============================================================================
# STUN/TURN Configuration (STUN/TURN服务器配置)
# =============================================================================
stunServer=stun.l.google.com      # STUN服务器地址
stunPort=19302                    # STUN服务器端口
turnServer=tx.fy403.cn            # TURN中继服务器地址（留空则禁用）
turnPort=3478                     # TURN服务器端口
turnUser=fy403                    # TURN服务器用户名
turnPass=qwertyuiop               # TURN服务器密码

# =============================================================================
# Video Configuration (视频配置)
# =============================================================================
videoDevice=/dev/video0          # 摄像头设备（USB: /dev/video0, MIPI CSI: /dev/video0）
videoType=                       # 摄像头传感器类型（imx219/ov5647，USB摄像头留空）
videoFormat=                       # 视频输入格式（留空则自动检测，如mjpeg/yuyv422）
videoCodec=h264                   # 视频编码：h264 / h265 / h264_rkmpp(硬编码) / hevc_rkmpp(硬编码)
resolution=1920x1080              # 视频分辨率
framerate=30                      # 视频帧率
profile=lowlatency                # 编码场景：lowlatency（低延时）/ hd（高清）

# =============================================================================
# Audio Input Configuration (音频输入配置)
# =============================================================================
audioDevice=hw:CARD=Audio,DEV=0   # 音频输入设备（留空则禁用音频采集）
audioFormat=S16_LE                # 音频输入格式
sampleRate=48000                  # 音频采样率
channels=1                        # 音频声道数：1=单声道，2=立体声

# =============================================================================
# Audio Output Configuration (音频输出配置)
# =============================================================================
speakerDevice=                    # 音频播放设备（留空则禁用音频播放）
outSampleRate=48000               # 音频输出采样率
outChannels=2                     # 音频输出声道数
volume=1.0                        # 音频输出音量（0.0~1.0）

# =============================================================================
# Debug Configuration (调试配置)
# =============================================================================
debug=false                       # 启用调试模式
```

#### 5.3 配置数据控制端（data_track）

编辑 `data_track/config.txt` 配置文件：

```ini
# =============================================================================
# Signaling Server Configuration (信令服务器配置)
# =============================================================================
webSocketServer=fy403.cn          # 信令服务器地址
webSocketPort=8000                # 信令服务器端口
client_id=dataTrack               # ⚠️ 必须全局唯一

# =============================================================================
# STUN/TURN Configuration (STUN/TURN服务器配置)
# =============================================================================
stunServer=stun.l.google.com      # STUN服务器地址
stunPort=19302                    # STUN服务器端口
turnServer=tx.fy403.cn            # TURN中继服务器地址
turnPort=3478                     # TURN服务器端口
turnUser=fy403                    # TURN服务器用户名
turnPass=qwertyuiop               # TURN服务器密码

# =============================================================================
# Motor Controller Configuration (电机控制器配置)
# 协议层统一为 raw PWM 1000~2000μs（中位1500）
# =============================================================================
usbDevice=/dev/ttyS5              # 电机驱动板串口设备
ttyBaudrate=420000                # 串口波特率
motorDriverType=crsf              # 电机驱动类型：crsf / pwm / dummy(调试用)

# =============================================================================
# CRSF 驱动参数（motorDriverType=crsf 时有效）
# 每个通道可单独设置中位值，默认均为 1500us
# =============================================================================
# crsfNeutralPwm_1=1500           # CH1 中位值 (us)
# crsfNeutralPwm_2=1500           # CH2 中位值 (us)
# ...                              # 支持 CH3 ~ CH16

# =============================================================================
# PWM 驱动参数（motorDriverType=pwm 时有效）
# 前后和左右可单独设置中位值，默认均为 1500us
# =============================================================================
# pwmFrontBackNeutralPwm=1500     # 前后通道中位值 (us)
# pwmLeftRightNeutralPwm=1500     # 左右通道中位值 (us)
# pwmFrontBackChip=0              # 前后控制PWM芯片编号
# pwmLeftRightChip=1              # 左右转向PWM芯片编号
# pwmFrontBackChannel=0           # 前后控制PWM通道
# pwmLeftRightChannel=0           # 左右转向PWM通道

# =============================================================================
# GPS Module Configuration (GPS模块配置) 必须支持NMEA协议
# =============================================================================
# gpsPort=/dev/ttyS5              # GPS模块串口设备（可选）
# gpsBaudrate=38400               # GPS模块波特率

# =============================================================================
# Restart Configuration (重启配置)
# =============================================================================
CHECK_INTERVAL=2                  # 健康检查间隔（秒）
```

#### 5.4 配置命令终端（cmd_track）

编辑 `cmd_track/config.txt` 配置文件：

```ini
# =============================================================================
# Signaling Server Configuration (信令服务器配置)
# =============================================================================
webSocketServer=fy403.cn          # 信令服务器地址
webSocketPort=8000                # 信令服务器端口
client_id=cmd_Terminal            # ⚠️ 必须全局唯一

# =============================================================================
# STUN/TURN Configuration (STUN/TURN服务器配置)
# =============================================================================
stunServer=stun.l.google.com      # STUN服务器地址
stunPort=19302                    # STUN服务器端口
turnServer=tx.fy403.cn            # TURN中继服务器地址
turnPort=3478                     # TURN服务器端口
turnUser=fy403                    # TURN服务器用户名
turnPass=qwertyuiop               # TURN服务器密码
```

#### 5.5 快速重置 CLIENT_ID

在项目根目录运行以下脚本，自动为三个模块生成随机唯一 ID 并更新 `config.txt` 文件：

```shell
#!/bin/bash
RANDOM_CAM=$(tr -dc 'a-zA-Z0-9' < /dev/urandom | head -c 8)
RANDOM_DATA=$(tr -dc 'a-zA-Z0-9' < /dev/urandom | head -c 8)
RANDOM_CMD=$(tr -dc 'a-zA-Z0-9' < /dev/urandom | head -c 8)

# 更新 config.txt
sed -i "s/^client_id=.*/client_id=cam_id_${RANDOM_CAM}/" av_track/config.txt
sed -i "s/^client_id=.*/client_id=data_id_${RANDOM_DATA}/" data_track/config.txt
sed -i "s/^client_id=.*/client_id=cmd_id_${RANDOM_CMD}/" cmd_track/config.txt
```

#### 5.6 无摄像头调试模式（fake camera）

如果没有物理摄像头，可使用 FFmpeg 的 `lavfi` 虚拟设备模拟摄像头：

```ini
# av_track/config.txt
videoDevice=fake                  # 启用模拟摄像头
videoFormat=                       # 留空即可
videoCodec=h264
resolution=640x480
framerate=30
```

**效果**：彩色测试图案（`testsrc`）+ 左上角毫秒时间戳。

自定义模拟画面（修改 `av_track/src/video_capturer.cpp`）：

| 测试源 | 效果 |
|--------|------|
| `testsrc` | 彩色条纹 + 滚动圆点（默认） |
| `color=c=blue` | 纯色背景 |
| `mandelbrot` | 曼德博分形动画 |
| `life` | 生命游戏 |

---

### Step 6：启动服务

**方式一：直接运行**

```shell
# 音视频采集端
cd av_track/build
./webrtc_publisher ../config.txt

# 数据控制端
cd data_track/build
./webrtc_publisher ../config.txt

# 命令终端（可选）
cd cmd_track/build
./cmd_shell ../config.txt
```

**方式二：使用安装脚本**

```shell
cd av_track   && ./install
cd data_track && ./install
cd cmd_track  && ./install
```

**方式三：Docker 一键运行**

```shell
sudo systemctl enable docker
docker pull alifys/ubuntu:arm64
./build-all.sh

cd av_track   && ./run-docker.sh config.txt
cd data_track && ./run-docker.sh config.txt
cd cmd_track  && ./run-docker.sh config.txt
```

---

### Step 7：打开控制端网页

用浏览器打开 [`web/index.html`](web/index.html)，或访问[在线体验](http://car.fy403.cn/)。

点击右上角 ⚙️ 齿轮图标可修改信令服务器地址和 STUN/TURN 配置，所有参数持久化到 localStorage。

**连接步骤：**

1. 确保 `av_track`、`data_track`、`cmd_track`（可选）已启动
2. 在浏览器 VIDEO LINK 面板输入 `av_track` 的 `client_id`，点击 CONNECT
3. 在 DATA LINK 面板输入 `data_track` 的 `client_id`，点击 CONNECT
4. 在 CMD LINK 面板输入 `cmd_track` 的 `client_id`，点击 CONNECT（可选）

等待画面和信号连接正常后即可操控：

**操控方式：**

| 控制器 | 触发方式 | 说明 |
|--------|----------|------|
| ⌨️ 键盘 | 自动激活 | W/S 前进/后退，A/D 左转/右转，空格急停 |
| 🎮 Xbox 手柄 | 插上自动识别 | 左摇杆方向，右扳机油门 |
| 📱 虚拟摇杆 | 移动端自动切换 | 触摸拖拽控制 |
| 🔄 陀螺仪 | 移动端倾斜手机 | 控制方向 |
| 🖥️ Shell 终端 | 底部面板 | 远程命令执行，Ctrl+C 中断 |

**通道按键绑定：**

点击16通道柱状图中的任意通道，弹出绑定设置窗：
- **Single 模式**：一个按键绑定 开/关 值
- **Continuous 模式**：两个按键绑定 最小值/最大值（持续按住渐变）

每个通道可独立选择速度曲线和反向开关。

---

## 材料清单

| 名称 | 参考价格 | 选购建议 |
|------|----------|----------|
| 开发板（如 OrangePi Zero2W / Rockchip RK3588） | ¥125~400 | **优先选带 MPP 硬件编解码的 Rockchip 型号** |
| 四路电机驱动器 / CRSF-PWM 转换器 | ¥40 | CRSF-PWM 方案适合 RC 遥控车无损改造 |
| USB 摄像头 / MIPI CSI 摄像头 | ¥40~80 | 推荐 MIPI CSI，低延时 + 免 CPU 采集 |
| 4G 网络模块 | ¥20 | 优先选支持 RNDIS 的模块，即插即用 |
| RC 遥控车 | ¥160 | 选方便改造的型号，预留安装空间 |
| 7.4V 锂电池 | ¥10 | 选大容量，注意尺寸 |
| USB 扩展线 | ¥12 | 选模块化设计，便于布线 |
| 杜邦线若干 | ¥20 | 备用连接线 |
| **合计** | **≈¥427~700** | — |

---

## 硬件选型指南

### 开发板

优先选择**带 Rockchip MPP 硬件编解码**的开发板（如 RK3588、RK3568 系列），启用 `h264_rkmpp` 后可大幅降低 CPU 占用：


- 纯软编码性能参考（4核1G，如 OrangePi Zero2）：CPU 平均占用 ~20%，延时 ~110ms
- Rockchip MPP 硬编码：CPU 占用可降至 5% 以下，延时 <100ms
- 摄像头输入格式建议选 `YUYV422`，避免使用 `MJPEG`（解压消耗 CPU 较大）

### 电机驱动方案

**方案一：CRSF-PWM 转换器**（RC 遥控车无损改造首选）

直接替换原遥控接收机，完全不改动车辆原有线路。通过串口（UART）通信，支持 16 个通道独立配置中位值。

<img src="README.assets/image-20251214120955129.png" alt="CRSF转换器" width="40%" />

配置方式：

```shell
motorDriverType=crsf
```

必要时修改 `data_track/config.txt` 中各通道中位值：

```ini
# 例如调整通道1中位值为 1520
crsfNeutralPwm_1=1520
```

**方案二：原生 GPIO PWM**（需要 Rockchip 开发板 PWM 控制器）

通过 Rockchip 芯片自带的 PWM 控制器直接输出 PWM 信号控制电调/舵机：

```shell
motorDriverType=pwm
```

PWM 驱动参数说明：

```ini
pwmFrontBackNeutralPwm=1500     # 前后通道中位值 (us)
pwmLeftRightNeutralPwm=1500     # 左右通道中位值 (us)
pwmFrontBackChip=0              # PWM芯片编号
pwmLeftRightChip=1              # PWM芯片编号
pwmFrontBackChannel=0           # PWM通道
pwmLeftRightChannel=0           # PWM通道
pwmPeriodNs=20000000            # PWM周期，默认20ms=50Hz
```

### 网络模块

| 方案 | 说明 | 推荐度 |
|------|------|--------|
| ECM/RNDIS 4G 模块 | 即插即用，识别为网卡|⭐⭐⭐ |
| USB WiFi 网卡 | 轻量、低成本，适合室内固定场景 | ⭐⭐ |
| 板载 WiFi | 开箱即用，无额外成本 | ⭐⭐ |

### 摄像头模块

| 方案 | 带宽 | 优点 | 局限 |
|------|------|------|------|
| MIPI CSI 摄像头 | 远超 USB | 低功耗、低延时、大带宽，可配置传感器帧率 | 需确认开发板接口兼容性 |
| USB 摄像头（MJPEG） | USB 2.0，480Mbps | 免驱，通用性强 | 带宽有限，高分辨率受限 |

> 💡 V2 提供 `camera/` 目录下的 CSI 摄像头配置工具，支持 OV5647 和 IMX219 两种传感器，可独立配置分辨率与帧率。推荐 MIPI CSI，配合开发板硬件编解码延时表现最佳。该目录只针对radxa zero 3w不兼容上面两种摄像头，其他开发板忽略！

### 声音模块

麦克风根据开发板接口选择，优先选带降噪功能的。扬声器推荐 USB 接口，扩展性好，只要能定位到具体设备参数即可。

---

## 项目 Q&A

### Q1：如何添加自定义的电机驱动方案？

可使用 AI Agent（CodeBuddy / Cursor / Lingma）以 Agent 模式完成，输入如下提示词（替换 `<your_method>`）：

```
假如你是本项目的 C++ 开发工程师，帮我添加一个新的 motor_driver：(<your_method>)。
你可以查看已有的驱动编写模板：@include/motor_driver.h。
驱动编写完成后，将其加入到 @src/MotorController 中。
```

例如，使用 GPIO PWM 控制电调：

```
假如你是本项目的 C++ 开发工程师，需要新增一个电机驱动模块。
该模块通过 GPIO 引脚输出 PWM 信号控制电调，工作频率 50Hz（周期 20ms）。

电调初始化流程：
1. 发送中立位脉冲（1500μs），使舵机归中；
2. 保持信号 2 秒，完成电调校准；
3. 校准完成后，控制量程为 900μs ~ 2100μs（以 1500μs 为零点）。

参考接口定义：@include/motor_driver.h
```

### Q2：必须购买所有配件吗？

不需要。系统高度模块化，可按需选配：

- **只需图传**：单独运行 `av_track`，不需要数据控制和命令终端
- **只需远程控制**：单独运行 `data_track`，不需要摄像头
- **只需远程命令**：单独运行 `cmd_track`，可远程执行 Shell 命令
- **开发板选择**：只要能运行 Linux + FFmpeg + C++ 即可，支持任意架构
- **电机驱动**：CRSF 和 PWM 两种方案均支持高度扩展

### Q3：图传延时能达到多少？

| 场景 | 延时 |
|------|------|
| 内网，软编码（4核1G 开发板） | ~110ms |
| 内网，Rockchip MPP 硬编码 | 可 <100ms |
| 4G 广域网 | 受网络波动影响，通常 150~300ms |

> 使用 MJPEG 输入格式可降低采集端 CPU 压力；使用 MIPI CSI 摄像头配合硬件编码可进一步降低延时。

### Q4：V2 协议与 V1 如何兼容？

V2 的 RC Protocol 是全新的 16 通道固定帧格式，与 V1 不兼容。如需从 V1 升级，需要：
1. 更新 `data_track` 和前端代码至 V2 版本
2. 在 `data_track/config.txt` 中重新配置电机驱动参数
3. 前端通道绑定需重新设置（因从2通道扩展到16通道）

### Q5：cmd_track 远程终端有什么用？

`cmd_track` 通过独立的 WebRTC 连接建立 Shell 终端隧道，主要用途：
- 远程查看开发板运行状态（`htop`, `ifconfig`, `dmesg`）
- 重启 `av_track` 或 `data_track` 服务（浏览器界面提供快捷按钮）
- 调试网络连通性（`ping`, `curl`）
- 无需额外 SSH 端口转发或 VPN

---

## 后续计划

- [x] 控制端语音推送
- [x] 被控端扬声器播放
- [x] H.265 编码支持
- [x] Rockchip MPP 硬件编码（h264_rkmpp / hevc_rkmpp）
- [x] 多控制端并行接入
- [x] 配置信息持久化
- [x] 多通道绑定
- [x] GNSS 定位
- [x] 16通道 RC Protocol V2
- [x] 速度响应曲线编辑器
- [x] 多控制器融合（键盘/Xbox/虚拟摇杆/陀螺仪）
- [x] 远程 Shell 终端（cmd_track）
- [x] MIPI CSI 摄像头配置工具
- [ ] 电子陀螺仪稳像
- [ ] 无线充电支持
- [ ] 编码器电机闭环控制
- [ ] AI 模型接入（目标识别 / 自主避障）
