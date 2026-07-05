# 面向低延时，点对点、远程音视频与信号控制的远距离遥控车系统

![License](https://img.shields.io/badge/license-MIT-yellow) ![Language](https://img.shields.io/badge/language-C++-blue) ![Language](https://img.shields.io/badge/language-HTML-blue)

![WebRTC](https://img.shields.io/badge/WebRTC-4682B4)![P2P](https://img.shields.io/badge/P2P-FF6347)![Linux](https://img.shields.io/badge/Linux-5F9EA0)![C++](https://img.shields.io/badge/C++-6495ED)![HTML](https://img.shields.io/badge/HTML-6495ED)![FFmpeg](https://img.shields.io/badge/FFmpeg-8A2BE2)![H264](https://img.shields.io/badge/H264-8A2BE2)![H265](https://img.shields.io/badge/H265-9370DB)![Opus](https://img.shields.io/badge/Opus-8A2BE2)![RKMPP](https://img.shields.io/badge/RKMPP-FF4500)![多线程](https://img.shields.io/badge/多线程-8A2BE2)![RNDIS](https://img.shields.io/badge/RNDIS-8A2BE2)![MIPI](https://img.shields.io/badge/MIPI_CSI-6A5ACD)![xterm.js](https://img.shields.io/badge/xterm.js-7B68EE)![4G网络](https://img.shields.io/badge/4G网络-9370DB)![开发板](https://img.shields.io/badge/开发板-8B008B)![电机驱动器](https://img.shields.io/badge/电机驱动器-BA55D3)![RC遥控车](https://img.shields.io/badge/RC遥控车-FF6347)

### ⌛欢迎star✨任何问题发issue👨‍🏫欢迎一起贡献代码🎊[源码地址](https://github.com/fy403/webrtc)

## 项目初衷

本项目是一个个人学习性质的开源项目，旨在系统学习和实践音视频开发相关知识，并结合实际编程语言提高动手能力。在整个系统设计与实现过程中，注重代码的可扩展性，每个功能模块都尽量以独立函数库的形式呈现，便于后续复用与拓展。同时，在代码实现上追求简洁清晰，剔除冗余逻辑，专注于保留最核心、最本质的实现内容，帮助理解底层原理。

## 项目介绍

项目主要面向RC遥控车改造，希望在遥控车基础上开发低延时、点对点、可远程控制、可捕获画面和声音的遥控车。采用的技术方案是通过RTP将采集的音视频或监控数据传输到控制端；控制信号也通过RTP传输到被控端，被控端解析协议转换为命令控制遥控车。技术方案尽可能减少服务器的参与，流量直接点对点传输。

**V2 版本重大升级：**
- 🆕 **16通道 RC 协议** — 全新 RC Protocol V2，支持16路独立通道，raw PWM 1000~2000μs，71字节固定帧
- 🆕 **多控制器融合** — 支持键盘、Xbox手柄、虚拟摇杆、手机陀螺仪四种操控方式
- 🆕 **远程 Shell 终端** — 新增 `cmd_track` 模块，通过 WebRTC DataChannel 实现远端开发板命令执行
- 🆕 **速度响应曲线** — 可视化曲线编辑器，为每个通道自定义非线性控制手感
- 🆕 **硬件编码加速** — 支持 Rockchip MPP 硬编码（`h264_rkmpp` / `hevc_rkmpp`），显著降低CPU占用
- 🆕 **MIPI CSI 摄像头** — 新增摄像头配置工具，支持 OV5647 / IMX219 等 CSI 摄像头的分辨率/帧率配置
- 🆕 **实时性能监控** — 前端 OSD 显示 FPS、码率、延时、Jitter、Codec、分辨率，CPU/内存仪表盘

## 系统架构

整套系统由四个核心部分组成：

**1. 信令服务器（Signaling Server）** — 负责 WebRTC 握手阶段 SDP/ICE 交换，使用 WebSocket 通信。

**2. 音视频采集端（`av_track`）** — 运行在开发板上，通过 FFmpeg 采集摄像头（USB / MIPI CSI）与麦克风数据，编码为 H.264/H.265 + Opus，经 WebRTC RTP 推送至浏览器。

**3. 数据控制端（`data_track`）** — 接收浏览器控制帧（RC Protocol V2，16通道），解析后通过串口驱动电机控制板（支持 CRSF-PWM / 原生 GPIO PWM）。同时采集 GPS、速度、电量、系统状态等遥测数据回传。

**4. 命令终端（`cmd_track`）** — 通过独立 WebRTC DataChannel 隧道传输 Shell 命令，浏览器端使用 xterm.js 渲染远程终端，支持 `Ctrl+C` 信号、容器重启等操作。

```
浏览器                      信令服务器                    开发板
  │                            │                          │
  │── WebSocket ──────────────►│◄── WebSocket ────────────│
  │                            │                          │
  │◄══════ P2P RTP (VIDEO LINK, av_track) ═══════════════►│
  │◄══════ P2P DataChannel (DATA LINK, data_track) ═══════►│
  │◄══════ P2P DataChannel (CMD LINK, cmd_track) ═════════►│
```

## 演示效果

O.S.D. 实时视频参数（FPS/码率/延时/Jitter/Decode/Codec/分辨率）；速度仪表盘；油门比例盘；CPU/内存指示器；16通道柱状图；GPS雷达定位；连接状态面板（Video/Data/CMD三路独立）；速度曲线编辑器；Shell远程终端。

[在线体验地址](http://car.fy403.cn/)
[B站视频](https://www.bilibili.com/video/BV1VA1kBnEAx?share_source=copy_web)

## 快速运行

> 💡 详细部署步骤、硬件选型、设备参数获取方法请查阅 [详细文档](README-Detail.md)

### 1. 服务器部分

#### 1.1 信令服务器（调试时可用作者搭建的，默认已包含在配置中）

信令服务器负责协助 WebRTC 双方交换 SDP 和 ICE 信息。支持 4G 远程只需一台有公网 IP 的轻量服务器（约70元/年），带宽要求极低。

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

> 默认端口 **8000**，记得在服务器防火墙放行 `TCP:8000`。

#### 1.2 STUN/TURN 服务器

公共 STUN 直接用 `stun.l.google.com:19302`。TURN 可自建（参考 [搭建私有TURN服务器](turn_server/README.md)）或使用 [Cloudflare TURN](https://pidan.dev/20250722/webrtc-livekit-deploy-config-turn-server/)（免费，延迟略高）。

### 2. 安装启动

#### 2.1.1 方式一：手动全量编译

```shell
cd webrtc
./build-all.sh
```

#### 2.1.2分模块编译

```shell
cd av_track   && chmod +x build.sh install.sh && ./build.sh
cd data_track && chmod +x build.sh install.sh && ./build.sh
cd cmd_track  && chmod +x build.sh install.sh && ./build.sh
```

#### 2.1.2启动服务

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

#### 2.2.1 方式二：Docker 一键运行（推荐）

```shell
# 拉取 ARM64 镜像
docker pull alifys/ubuntu:arm64

# 根目录构建
./build-all.sh

# 运行各模块（必须指定配置文件）
cd av_track   && ./run-docker.sh config.txt
cd data_track && ./run-docker.sh config.txt
cd cmd_track  && ./run-docker.sh config.txt
```

### 3. 打开控制端网页

浏览器打开 [`web/index.html`](web/index.html)，或访问[在线体验](http://car.fy403.cn/)。

点击右上角 ⚙️ 齿轮图标可修改信令服务器地址和 STUN/TURN 配置。

**操控方式：**

| 控制器 | 说明 |
|--------|------|
| ⌨️ 键盘 | W/S 前进/后退，A/D 左转/右转，空格急停（支持按键自定义绑定） |
| 🎮 Xbox 手柄 | 即插即用，摇杆控制，支持 RT 扳机 |
| 📱 虚拟摇杆 | 触摸屏友好，移动端自动切换 |
| 🔄 陀螺仪 | 手机倾斜控制（移动端专属） |
| 📈 速度曲线 | 自定义非线性响应曲线，每个通道独立配置 |
| 🖥️ Shell 终端 | 远程命令执行，Ctrl+C 中断，容器重启 |

## 后续计划

- [x] 控制端语音推送
- [x] 被控端扬声器播放
- [x] H.265 编码支持
- [x] 多控制端并行接入
- [x] 配置信息持久化
- [x] 多通道绑定
- [x] GNSS 定位 + 惯性导航（IMU）
- [x] 16通道 RC Protocol V2
- [x] 远程 Shell 终端（cmd_track）
- [x] 速度响应曲线编辑器
- [x] 多控制器融合（键盘/Xbox/虚拟摇杆/陀螺仪）
- [x] Rockchip MPP 硬件编码加速
- [x] MIPI CSI 摄像头配置工具
- [ ] 电子陀螺仪稳像
- [ ] 无线充电支持
- [ ] 编码器电机闭环控制
- [ ] AI 模型接入（目标识别 / 自主避障）

## QQ群交流

<img src="README.assets/qrcode_1764133405428.jpg" alt="qrcode_1764133405428" style="zoom: 30%;" />
