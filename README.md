# 面向低延时，点对点、远程音视频与信号控制的远距离遥控车系统

![License](https://img.shields.io/badge/license-MIT-yellow) ![Language](https://img.shields.io/badge/language-C++-blue) ![Language](https://img.shields.io/badge/language-HTML-blue)

![WebRTC](https://img.shields.io/badge/WebRTC-4682B4)![P2P](https://img.shields.io/badge/P2P-FF6347)![Linux](https://img.shields.io/badge/Linux-5F9EA0)![C++](https://img.shields.io/badge/C++-6495ED)![HTML](https://img.shields.io/badge/HTML-6495ED)![FFmpeg](https://img.shields.io/badge/FFmpeg-8A2BE2)![H264](https://img.shields.io/badge/H264-8A2BE2)![Opus](https://img.shields.io/badge/Opus-8A2BE2)![多线程](https://img.shields.io/badge/多线程-8A2BE2)![RNDIS](https://img.shields.io/badge/RNDIS-8A2BE2)![USB摄像头](https://img.shields.io/badge/USB摄像头-6A5ACD)![USB麦克风](https://img.shields.io/badge/USB麦克风-7B68EE)![4G网络](https://img.shields.io/badge/4G网络-9370DB)![WIFI](https://img.shields.io/badge/WIFI-9932CC)![开发板](https://img.shields.io/badge/开发板-8B008B)![电机驱动器](https://img.shields.io/badge/电机驱动器-BA55D3)![RC遥控车](https://img.shields.io/badge/RC遥控车-FF6347)

### ⌛欢迎star✨任何问题发issue👨‍🏫欢迎一起贡献代码🎊[源码地址](https://github.com/fy403/webrtc)

[TOC]

## 项目初衷

 **本项目是一个个人学习性质的开源项目，旨在系统学习和实践音视频开发相关知识，并结合实际编程语言提高动手能力。在整个系统设计与实现过程中，注重代码的可扩展性，每个功能模块都尽量以独立函数库的形式呈现，便于后续复用与拓展。同时，在代码实现上追求简洁清晰，剔除冗余逻辑，专注于保留最核心、最本质的实现内容，帮助理解底层原理。**

## 项目介绍

**项目主要面向RC遥控车改造，希望在遥控车基础上开发低延时、点对点、可远程控制、可捕获画面和声音的遥控车。采用的技术方案是通过RTP将采集的音视频或者是监控数据传输到控制端；将控制信号也通过RTP传输到被控遥控车，遥控车通过解析协议，转换为命令控制遥控车。技术方案尽可能减少服务器的参与，流量直接点对点传输。**

## 演示效果

-----------

屏幕OSD信息；数据通道实时数据变化；连接状态情况；实时视频参数调整。

<img src="README.assets\image-20260128185224269.png" alt="image-20260128185224269" style="zoom: 54%;" />

<img src="README.assets\image-20260128185304740.png" alt="image-20260128185304740" style="zoom: 50%;" />

<img src="README.assets\image-20260128185324642.png" alt="image-20260128185324642" style="zoom:50%;" />

------
<iframe src="//player.bilibili.com/player.html?isOutside=true&aid=115489808323280&bvid=BV1VA1kBnEAx&cid=33681311377&page=1&high_quality=1&danmaku=0" allowfullscreen="allowfullscreen" width="100%" height="500" scrolling="no" frameborder="0" sandbox="allow-top-navigation allow-same-origin allow-forms allow-scripts"></iframe>

[B站视频](https://www.bilibili.com/video/BV1VA1kBnEAx?share_source=copy_web)
-------

## 项目Q&A

### Q1：如果我用的电机驱动方案不存在本项目中，如何驱动电机工作？

A1: 可以自己实现电机驱动方案，比如使用PWM控制电机，或者使用GPIO控制电机。如果不会编写代码，可以使用CodeBuddy或者Cursor，或者VSCODE的Lingma插件，开启Agent模式。输入以下提示词，替换掉`<your_method>`为实际电机驱动方法。

```txt
假如你是本项目的C++开发工程师，帮我添加一个新的motor_driver：(<your_method>)，你可以查看已有的驱动编写模板代码：@include/uart_motor_driver.h @include/motor_driver.h 。
驱动编写完成后，将其加入到@src/MotorController的构造函数中。你可以阅读整体代码，实现该功能编写。
```

例如：使用pwm控制电调：

```markdown

假如你是本项目的C++开发工程师，需要新增一个电机驱动模块。该模块通过**GPIO引脚22**输出**PWM信号**控制**电调**，其**工作频率**为**50Hz（周期20ms）**。

电调初始化流程如下：
1. 发送**中立位脉冲（1500μs）**，使舵机归中。
2. 保持该信号**2秒**，完成电调校准（校准成功后会发出**滴滴提示音**）。
3. 校准完成后，控制量程为 **900μs 至 +2100μs**（以1500μs为零点）。

你可参考以下现有驱动的代码结构进行实现：
- 接口定义：`@include/motor_driver.h`
- 示例实现：`@include/uart_motor_driver.h`

驱动开发完成后，请将其集成到 **`@src/MotorController` 的构造函数**中，并确保与现有系统兼容。建议先理解整体代码架构，再进行模块化开发。
```

### Q2：:需要全部购买配件吗？

A2：不需要，每个部分都是非常强可扩展的。开发板只要能够上网，且能够编译运行ffmpeg以及C++代码就行，无论什么系统。如果只需要图传部分，可用单独运行av_track。如果只需要数字控制部分可只运行data_track。电机驱动器支持两种高可扩展方案（将文档后面说明），支持RC遥控车无损话改造。

### Q3：图传延时怎么样？

A3: 目前最新的图传延时能够在内网环境下达到110ms，且没有使用编码芯片支持。纯使用CPU的软编码，如果开发板硬件比较好，理论可以降低到100ms以下。

-----

## 快速运行

### 1.依赖安装（ubuntu为例）

#### 1.1服务器部分：信令服务器搭建（调试时可用我搭建的, 默认包含在配置中)

信令服务器主要是协助WebRTC双方交换SDP（音视频参数信息）以及ICE（双方网络地址信息）。所以要想支持4G远程，需要购买一个便宜的建站服务器就行，成本70/年(2H2G)。服务器需要有公网ip，服务器不需要很大的带宽（不负责传输流媒体数据）。

##### 下载项目

```shel
git clone https://github.com/fy403/webrtc
```

##### 依赖安装

方法1：nodejs

```shell
cd webrtc/signaling_server/nodejs
# 安装nodejs, npm
sudo apt install nodejs npm
# 安装依赖
npm install 
```

方法2：python3

```shell
cd webrtc/signaling_server/python3
# 安装python3, pip3
sudo apt install python3 python3-pip
pip3 install -r requirements.txt
```

##### 启动信令服务器

```shel
chmod +x ./install && ./install
```

> 默认端口是8000, 记得服务器允许防火墙TCP:8000端口流量通过。
> 如果fy403.cn:8000还能用，那么就不用搭建信令服务器了。直接用我的就行，但是记住一定要配置唯一的`CLIENT_ID="cam_id_YvgpEqD4" # 客户端ID`。【如果冲突，请自行修改】

#### 1.2 服务器部分：STUN/TURN服务器准备（调试时可用我搭建的, 默认包含在配置中))

STUN/TRUN服务器是用来获取WebRTC双方的网络地址信息：本地接口直接地址（Host Candidate）、STUN服务器反射地址（Server Reflexive Candidate）以及TURN中继地址（Relayed Candidate）。通常内网地址优先级低于公网直连地址。如果内网不能互通则走中级TURN服务器，流媒体流量从TURN转发。

公开的STUN服务器非常多，比如：(stun_host=‘stun.l.google.com’, stun_port=19302)。

TURN服务器可以自己搭建[搭建私有TURN服务器](turn_server/README.md)，当然也可以使用cloudflare免费提供的服务器（但是延迟会比较大，且不稳定）。具体可以参考这篇Blog[【WebRTC全流程】livekit配置免费的cloudflare turn服务 - PiDan! | 虚拟世界的懒猫的博客](https://pidan.dev/20250722/webrtc-livekit-deploy-config-turn-server/)

申请后，只需要通过他提供的一个cmd命令curl获取TURN server host和username，password就行。

#### 1.3 控制板部分(Docker一键安装)
> 详细细节请查看[Detail](README-Detail.md)


### 安装启动
```shell
# 配置开机启动Docker
sudo systemctl enable docker
# 从网盘下载Docker镜像()
# 通过网盘分享的文件：alifys_ubuntu_arm64.tar.gz
# 链接: https://pan.baidu.com/s/1KUMbif2980GnhZgIphmZ5g?pwd=fy43 提取码: fy43

# 解压
gzip -d -c alifys_ubuntu_arm64.tar.gz > alifys_ubuntu_arm64.tar
# 导入镜像
# 加载tar文件并获取镜像ID，然后直接打标签
docker import alifys_ubuntu_arm64.tar alifys/ubuntu:arm64
```

```shell

#1. 使用根目录构建
./build-all.sh

#2. 运行 av_track
pushd av_track
./run-docker.sh

# 或者使用自定义参数运行
./run-docker.sh --device /dev/video0 --name cam_001 --client-id cam_001

#3. 运行 data_track
popd
pushd data_track
./run-docker.sh

# 或者使用自定义串口
./run-docker.sh --motor-port /dev/ttyUSB1 --gsm-port /dev/ttyACM1 --name data_001
popd
```

## QQ群交流

<img src="README.assets\qrcode_1764133405428.jpg" alt="qrcode_1764133405428" style="zoom: 50%;" />
