# 面向低延时，点对点、远程音视频与信号控制的远距离遥控车系统

![License](https://img.shields.io/badge/license-MIT-yellow) ![Language](https://img.shields.io/badge/language-C++-blue) ![Language](https://img.shields.io/badge/language-HTML-blue)

![WebRTC](https://img.shields.io/badge/WebRTC-4682B4)![P2P](https://img.shields.io/badge/P2P-FF6347)![Linux](https://img.shields.io/badge/Linux-5F9EA0)![C++](https://img.shields.io/badge/C++-6495ED)![HTML](https://img.shields.io/badge/HTML-6495ED)![FFmpeg](https://img.shields.io/badge/FFmpeg-8A2BE2)![H264](https://img.shields.io/badge/H264-8A2BE2)![Opus](https://img.shields.io/badge/Opus-8A2BE2)![多线程](https://img.shields.io/badge/多线程-8A2BE2)![RNDIS](https://img.shields.io/badge/RNDIS-8A2BE2)![USB摄像头](https://img.shields.io/badge/USB摄像头-6A5ACD)![USB麦克风](https://img.shields.io/badge/USB麦克风-7B68EE)![4G网络](https://img.shields.io/badge/4G网络-9370DB)![WIFI](https://img.shields.io/badge/WIFI-9932CC)![开发板](https://img.shields.io/badge/开发板-8B008B)![电机驱动器](https://img.shields.io/badge/电机驱动器-BA55D3)![RC遥控车](https://img.shields.io/badge/RC遥控车-FF6347)

### ⌛欢迎star✨任何问题发issue👨‍🏫欢迎一起贡献代码🎊[源码地址](https://github.com/fy403/webrtc)

## 项目初衷

本项目是一个个人学习性质的开源项目，旨在系统学习和实践音视频开发相关知识，并结合实际编程语言提高动手能力。在整个系统设计与实现过程中，注重代码的可扩展性，每个功能模块都尽量以独立函数库的形式呈现，便于后续复用与拓展。同时，在代码实现上追求简洁清晰，剔除冗余逻辑，专注于保留最核心、最本质的实现内容，帮助理解底层原理。

## 项目介绍

项目主要面向RC遥控车改造，希望在遥控车基础上开发低延时、点对点、可远程控制、可捕获画面和声音的遥控车。采用的技术方案是通过RTP将采集的音视频或者是监控数据传输到控制端；将控制信号也通过RTP传输到被控遥控车，遥控车通过解析协议，转换为命令控制遥控车。技术方案尽可能减少服务器的参与，流量直接点对点传输。

## 数据流动示意

> 下图展示了从"你按下键盘"到"小车动起来"、从"摄像头拍到画面"到"浏览器显示"的完整数据流动过程。
> 服务器只在**建立连接时**短暂参与，连接建立后画面和控制信号均**直接点对点传输**，服务器不再转发任何流媒体数据。

```mermaid
sequenceDiagram
    autonumber
    participant 浏览器 as 🖥️ 浏览器<br/>（你的电脑）
    participant Signaling as ☁️ 信令服务器<br/>（牵线搭桥）
    participant STUN_TURN as ☁️ STUN / TURN<br/>（地址探测 / 中继备用）
    participant 控制板 as 🤖 控制板<br/>（小车上的大脑）
    participant 摄像头 as 📷 摄像头
    participant 麦克风 as 🎙️ 麦克风
    participant 扬声器 as 🔊 扬声器
    participant 电调 as ⚙️ 电调 / 电机

    rect rgb(230, 245, 255)
        Note over 浏览器,控制板: ① 握手阶段：互相告知"我在哪、我能说什么语言"（仅在连接建立时发生，之后服务器退场）
        浏览器->>Signaling: 我想连接小车，这是我的网络地址和能力（SDP + ICE）
        Signaling->>控制板: 转交给你，浏览器想跟你说话
        控制板->>STUN_TURN: 我的公网地址是多少？
        STUN_TURN-->>控制板: 你的公网地址是 x.x.x.x
        控制板->>Signaling: 好的，这是我的网络地址和能力（SDP + ICE）
        Signaling->>浏览器: 转交给你，小车回应了
        浏览器->>STUN_TURN: 我的公网地址是多少？
        STUN_TURN-->>浏览器: 你的公网地址是 y.y.y.y
        Note over 浏览器,控制板: 双方尝试直连 → 若防火墙阻断则通过 TURN 中继
        浏览器-->>控制板: ✅ P2P 通道建立成功（或经 TURN 中继）
    end

    rect rgb(255, 248, 220)
        Note over 摄像头,浏览器: ② 画面传输：摄像头拍到的画面 → 实时出现在你的浏览器
        摄像头->>控制板: 原始画面（YUV / MJPEG）
        控制板->>控制板: FFmpeg 压缩编码（H.264）
        控制板-->>浏览器: 📡 压缩后的视频流（RTP，走 P2P 通道）
        浏览器->>浏览器: 解码 → 显示画面
    end

    rect rgb(240, 255, 240)
        Note over 麦克风,浏览器: ③ 声音传输：小车周围的声音 → 实时出现在你的耳机
        麦克风->>控制板: 原始音频（PCM）
        控制板->>控制板: 编码压缩（Opus）
        控制板-->>浏览器: 📡 压缩后的音频流（RTP，走 P2P 通道）
        浏览器->>浏览器: 解码 → 播放声音
    end

    rect rgb(255, 240, 240)
        Note over 浏览器,电调: ④ 控制传输：你按下键盘 → 小车动起来（延时 ≤110ms）
        浏览器->>浏览器: 检测到按键（W/A/S/D/空格）
        浏览器-->>控制板: 📡 控制指令（DataChannel，走 P2P 通道）
        控制板->>控制板: 解析指令 → 转换为 PWM 信号
        控制板->>电调: 串口发送 PWM 指令
        电调->>电调: 驱动电机转动 🚗
    end

    rect rgb(245, 235, 255)
        Note over 浏览器,控制板: ⑤ 遥测回传：小车状态数据 → 实时显示在仪表盘
        控制板->>控制板: 采集 GPS / 速度 / 电量 / 加速度
        控制板-->>浏览器: 📡 遥测数据（DataChannel，走 P2P 通道）
        浏览器->>浏览器: 渲染仪表盘（速度、GPS、加速度曲线）
    end

    rect rgb(230, 255, 248)
        Note over 浏览器,扬声器: ⑥ 语音下发：你说的话 → 从小车扬声器播出
        浏览器->>浏览器: 采集麦克风 → 编码（Opus）
        浏览器-->>控制板: 📡 音频流（RTP，走 P2P 通道）
        控制板->>扬声器: 解码 → 播放
    end
```

## 演示效果

屏幕OSD信息；数据通道实时数据变化；连接状态情况；实时视频参数调整；加速度曲线；GPS定位；速度仪表盘；共享连接数。

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

方法1：nodejs

```shell
cd webrtc/signaling_server/nodejs
sudo apt install nodejs npm
npm install
chmod +x ./install && ./install
```

方法2：python3

```shell
cd webrtc/signaling_server/python3
sudo apt install python3 python3-pip
pip3 install -r requirements.txt
chmod +x ./install && ./install
```

> 默认端口是8000，记得在服务器防火墙放行 TCP:8000。
> 如果 `fy403.cn:8000` 还能用，可直接使用，但必须配置唯一的 `CLIENT_ID="cam_id_YvgpEqD4"`，避免冲突。

#### 1.2 STUN/TURN 服务器（调试时可用作者搭建的，默认已包含在配置中）

STUN/TURN 服务器用于获取 WebRTC 双方的网络地址信息。公开 STUN 服务器直接使用即可，如 `stun.l.google.com:19302`。

TURN 服务器可自建（参考 [搭建私有TURN服务器](turn_server/README.md)），也可使用 Cloudflare 免费提供的节点（延迟稍高）。

#### 1.3 控制板部分（Docker 一键安装）

> 详细细节请查看 [Detail](README-Detail.md)

### 2. 安装启动

```shell
# 配置开机启动 Docker
sudo systemctl enable docker
# 从dockerhub拉取镜像
docker pull alifys/ubuntu:arm64
```

```shell
# 1. 使用根目录构建
./build-all.sh

# 2. 运行 av_track
pushd av_track
./run-docker.sh
# 或使用自定义参数
./run-docker.sh  config.txt

# 3. 运行 data_track
popd
pushd data_track
./run-docker.sh
# 或使用自定义串口
./run-docker.sh config.txt
popd
```

## 后续计划

- [x] 控制端语音推送
- [x] 被控端扬声器播放
- [x] X265编码
- [x] 多控制端并行
- [x] 配置信息持久化
- [x] 多通道绑定
- [ ] GNSS定位+带惯导
- [ ] 电子陀螺仪
- [ ] 无线充电
- [ ] 视频编码芯片支持
- [ ] 编码器电机支持
- [ ] AI模型接入

## QQ群交流

<img src="README.assets/qrcode_1764133405428.jpg" alt="qrcode_1764133405428" style="zoom: 30%;" />
