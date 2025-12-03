# 面向低延时，点对点、远程音视频与信号控制的4G遥控车系统

![License](https://img.shields.io/badge/license-MIT-yellow) ![Language](https://img.shields.io/badge/language-C++-blue) ![Language](https://img.shields.io/badge/language-HTML-blue)

![WebRTC](https://img.shields.io/badge/WebRTC-4682B4)![P2P](https://img.shields.io/badge/P2P-FF6347)![Linux](https://img.shields.io/badge/Linux-5F9EA0)![C++](https://img.shields.io/badge/C++-6495ED)![HTML](https://img.shields.io/badge/HTML-6495ED)![FFmpeg](https://img.shields.io/badge/FFmpeg-8A2BE2)![H264](https://img.shields.io/badge/H264-8A2BE2)![Opus](https://img.shields.io/badge/Opus-8A2BE2)![多线程](https://img.shields.io/badge/多线程-8A2BE2)![RNDIS](https://img.shields.io/badge/RNDIS-8A2BE2)![USB摄像头](https://img.shields.io/badge/USB摄像头-6A5ACD)![USB麦克风](https://img.shields.io/badge/USB麦克风-7B68EE)![4G网络](https://img.shields.io/badge/4G网络-9370DB)![WIFI](https://img.shields.io/badge/WIFI-9932CC)![开发板](https://img.shields.io/badge/开发板-8B008B)![电机驱动器](https://img.shields.io/badge/电机驱动器-BA55D3)![RC遥控车](https://img.shields.io/badge/RC遥控车-FF6347)

### ⌛欢迎star✨任何问题发issue👨‍🏫欢迎一起贡献代码🎊[源码地址](https://github.com/fy403/webrtc)
[TOC]

## 项目初衷

 **本项目是一个个人学习性质的开源项目，旨在系统学习和实践音视频开发相关知识，并结合实际编程语言提高动手能力。在整个系统设计与实现过程中，注重代码的可扩展性，每个功能模块都尽量以独立函数库的形式呈现，便于后续复用与拓展。同时，在代码实现上追求简洁清晰，剔除冗余逻辑，专注于保留最核心、最本质的实现内容，帮助理解底层原理。**

## 项目介绍

**项目主要面向RC遥控车改造，希望在遥控车基础上开发低延时、点对点、可远程控制、可捕获画面和声音的遥控车。采用的技术方案是通过RTP将采集的音视频或者是监控数据传输到控制端；将控制信号也通过RTP传输到被控遥控车，遥控车通过解析协议，转换为命令控制遥控车。技术方案尽可能减少服务器的参与，流量直接点对点传输。**

## 演示效果

<iframe src="//player.bilibili.com/player.html?isOutside=true&aid=115489808323280&bvid=BV1VA1kBnEAx&cid=33681311377&page=1&high_quality=1&danmaku=0" allowfullscreen="allowfullscreen" width="100%" height="500" scrolling="no" frameborder="0" sandbox="allow-top-navigation allow-same-origin allow-forms allow-scripts"></iframe>

## 快速运行

### 1.依赖安装（ubuntu为例）

#### 1.1 信令服务器搭建

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
> 如果fy403.cn:8000还能用，那么就不用搭建信令服务器了。直接用我的就行，但是记住一定要配置唯一的`CLIENT_ID="usbcam" # 客户端ID`。【如果冲突，请自行修改】

#### 1.2 STUN/TURN服务器准备

STUN/TRUN服务器是用来获取WebRTC双方的网络地址信息：本地接口直接地址（Host Candidate）、STUN服务器反射地址（Server Reflexive Candidate）以及TURN中继地址（Relayed Candidate）。通常内网地址优先级低于公网直连地址。如果内网不能互通则走中级TURN服务器，流媒体流量从TURN转发。

公开的STUN服务器非常多，比如：(stun_host=‘stun.l.google.com’, stun_port=19302)。

TURN服务器可以自己搭建[搭建私有TURN服务器](turn_server/README.md)，当然也可以使用cloudflare免费提供的服务器（但是延迟会比较大，且不稳定）。具体可以参考这篇Blog[【WebRTC全流程】livekit配置免费的cloudflare turn服务 - PiDan! | 虚拟世界的懒猫的博客](https://pidan.dev/20250722/webrtc-livekit-deploy-config-turn-server/)

申请后，只需要通过他提供的一个cmd命令curl获取TURN server host和username，password就行。

#### 1.3 开发板依赖安装

```shel
sudo apt install g++ make cmake ffmpeg

# 手动安装libdatachannel
git clone https://github.com/paullouisageneau/libdatachannel.git
cd libdatachannel
git submodule update --init --recursive
mkdir build && cd build
cmake .. 
make -j2
sudo make install
```

> ffmpeg可能安装失败，建立手动编译安装
>
> 开发板通常是arm架构，可以换成清华的post源

### 2.设备查找

#### 2.1 摄像头参数获取

```shel
root@orangepizero2:~# sudo v4l2-ctl --list-device
cedrus (platform:cedrus):
        /dev/video0
        /dev/media0

SIT USB2.0 Camera RGB: SIT USB2 (usb-5200000.usb-1.2):
        /dev/video1 # 第一个摄像头
        /dev/video2
        /dev/media1
```
获取摄像头参数：视频格式，分辨率，帧率
```shell
root@orangepizero2:~# sudo v4l2-ctl -d /dev/video1 --list-formats-ext
ioctl: VIDIOC_ENUM_FMT
        Type: Video Capture

        [0]: 'MJPG' (Motion-JPEG, compressed) # 视频格式
                Size: Discrete 640x480 # 支持的分辨率
                        Interval: Discrete 0.033s (30.000 fps) # 各种帧率支持
                        Interval: Discrete 0.040s (25.000 fps)
                        Interval: Discrete 0.050s (20.000 fps)
                        Interval: Discrete 0.067s (15.000 fps)
                        Interval: Discrete 0.100s (10.000 fps)
                        Interval: Discrete 0.200s (5.000 fps)
                        Interval: Discrete 0.033s (30.000 fps)
                        Interval: Discrete 0.040s (25.000 fps)
                        Interval: Discrete 0.050s (20.000 fps)
                        Interval: Discrete 0.067s (15.000 fps)
                        Interval: Discrete 0.100s (10.000 fps)
                        Interval: Discrete 0.200s (5.000 fps)
                Size: Discrete 1920x1080
                ......

```

#### 2.2 麦克风参数获取

```shel
root@orangepizero2:~# arecord -L
null
    Discard all samples (playback) or generate zero samples (capture)
hw:CARD=ahubhdmi,DEV=0
    ahubhdmi, ahub_plat-i2s-hifi i2s-hifi-0
    Direct hardware device without any conversions
plughw:CARD=ahubhdmi,DEV=0
    ahubhdmi, ahub_plat-i2s-hifi i2s-hifi-0
    Hardware device with all software conversions
default:CARD=ahubhdmi
    ahubhdmi, ahub_plat-i2s-hifi i2s-hifi-0
    Default Audio Device
sysdefault:CARD=ahubhdmi
    ahubhdmi, ahub_plat-i2s-hifi i2s-hifi-0
    Default Audio Device
dsnoop:CARD=ahubhdmi,DEV=0
    ahubhdmi, ahub_plat-i2s-hifi i2s-hifi-0
    Direct sample snooping device
hw:CARD=Audio,DEV=0 # 第一个USB麦克风
    AB13X USB Audio, USB Audio
    Direct hardware device without any conversions
plughw:CARD=Audio,DEV=0
    AB13X USB Audio, USB Audio
    Hardware device with all software conversions
default:CARD=Audio
    AB13X USB Audio, USB Audio
    Default Audio Device
sysdefault:CARD=Audio
    AB13X USB Audio, USB Audio
    Default Audio Device
front:CARD=Audio,DEV=0
    AB13X USB Audio, USB Audio
    Front output / input
dsnoop:CARD=Audio,DEV=0
    AB13X USB Audio, USB Audio
    Direct sample snooping device
```
识别到后，就需要获取采样参数：采样率，音频格式，通道数。
```shell
root@orangepizero2:~# arecord --device=hw:CARD=Audio,DEV=0 --dump-hw-params
ACCESS:  MMAP_INTERLEAVED RW_INTERLEAVED
FORMAT:  S16_LE # 音频格式
SUBFORMAT:  STD
SAMPLE_BITS: 16
FRAME_BITS: 16
CHANNELS: 1 # 通道数
RATE: 48000 # 采样率
PERIOD_TIME: [1000 1000000]
PERIOD_SIZE: [48 48000]
PERIOD_BYTES: [96 96000]
PERIODS: [2 1024]
BUFFER_TIME: [2000 2000000]
BUFFER_SIZE: [96 96000]
BUFFER_BYTES: [192 192000]
TICK_TIME: ALL
--------------------
arecord: set_params:1352: Sample format non available
Available formats:
- S16_LE
```

#### 2.3 电机驱动器接口获取

```shel
root@orangepizero2:~# ls /dev/ttyUSB*
/dev/ttyUSB0
```


#### 2.4 4G网络模块接口获取
使用RNDIS的4G模块，通常第一个以enx开头的就是4G模块的网卡。然后查看路由信息，ping测试一下上网情况。

> 如果没有4G模块，直接配置开发板与电脑连接同一个WIFI也可以控制！

```shel
root@orangepizero2:~# ip a
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
    inet6 ::1/128 scope host
       valid_lft forever preferred_lft forever
5: enx2089846a96ab: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UNKNOWN group default qlen 1000
    link/ether 20:89:84:6a:96:ab brd ff:ff:ff:ff:ff:ff
    inet 192.168.10.2/24 brd 192.168.10.255 scope global dynamic noprefixroute 
       valid_lft 86396sec preferred_lft 86396sec
    inet6 xxxxxxxxxxxx/64 scope link noprefixroute
       valid_lft forever preferred_lft forever

root@orangepizero2:~# ip route
default via 192.168.10.1 dev enx2089846a96ab proto dhcp metric 100 # 4g网卡优先上网
```
获取对应的USB调试串口，通常来说是一个以ttyACM开头的第一个就是。
```shell
root@orangepizero2:~# ls /dev/ttyACM*
/dev/ttyACM0  /dev/ttyACM1  /dev/ttyACM2
```

### 3.安装启动

#### 3.1 编译程序

```shell
git clone https://github.com/fy403/webrtc

# 编译音视频采集程序
cd av_track
chmod +x ./build.sh 
chmod +x ./install.sh
./build.sh

# 编译控制信号传输程序
cd data_track
chmod +x ./build.sh
chmod +x ./install.sh
./build.sh
```

#### 3.2 运行安装脚本

如果使用不同的电机驱动端口，需要修改[constants.h](data_track/include/constants.h)中的前后左右控制端口，以及pwm行程。

```c++
// Motor control constants
const int MOTOR_FRONT_BACK = 2; // Control forward/backward
const int MOTOR_LEFT_RIGHT = 4; // Control left/right turn

const int16_t NEUTRAL_PWM = 0;
const int16_t MAX_FORWARD_PWM = 3500;
const int16_t MAX_REVERSE_PWM = -3500;

```
根据之前的获取的配置，修改av_track/av_track_rtc.sh文件，修改如下：
```shell
#!/bin/bash
TARGET_HOST="fy403.cn" # 信令服务器地址
TARGET_PORT=8000 # 信令服务器端口
IP_TYPE=4 # ipv4 or ipv6
VIDEO_DEVICE="/dev/video1" # 首选摄像头设备
VIDEO_DEVICE_BAK="/dev/video0" # 备选摄像头设备
AUDIO_DEVICE="hw:CARD=Audio,DEV=0" # 音频设备
SAMPLE_RATE=48000 # 音频采样率
CHANNELS=1 # 音频通道数
AUDIO_FORMAT="S16LE" # 音频格式
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="usbcam" # 客户端ID：不填写使用随机值
STUN_SERVER="stun.l.google.com" # STUN服务器地址
STUN_SERVER_PORT=19302 # STUN服务器端口
TURN_SERVER="turn.cloudflare.com" # TURN服务器地址
TURN_SERVER_PORT=3478 # TURN服务器端口
USER="g0xxxxxxxxxxx" # TURN服务器用户名
PASSWD="95yyyyyyyyy" # TURN服务器密码
RESOLUTION="1280x720" # 画面分辨率
FPS=20 # 画面帧率
```
同理，修改data_track/data_track_rtc.sh文件，修改如下：
```shell
#!/bin/bash
TARGET_HOST="fy403.cn" 
TARGET_PORT=8000
IP_TYPE=4
USER="g0xxxxxxxxxxx"
PASSWD="95yyyyyyyyy"
CHECK_INTERVAL=2  # Health check interval (seconds)
CLIENT_ID="dataTrack"
STUN_SERVER="stun.l.google.com"
STUN_SERVER_PORT=19302
TURN_SERVER="turn.cloudflare.com"
TURN_SERVER_PORT=3478
TTY_PORT=/dev/ttyUSB0 # 电机驱动板usb端口
TTY_BAUDRATE=115200 # 电机驱动板串口波特率
GSM_PORT=/dev/ttyACM0 # 4g模块usb端口
GSM_BAUDRATE=115200 # 4g模块串口波特率
```

修改完毕后直接允许每个模块下的`./install`就行安装了并启动了。

如果需要知道参数细节，或配置更多参数。可在build目录下使用`./web_publisher -h`查看info。
```shell
root@orangepizero2:~/webrtc/av_track/cmake-build-debug-zero2# ./webrtc_publisher -h
usage: ./webrtc_publisher [ -enstwxhv ]
libdatachannel client implementing WebRTC Data Channels with WebSocket signaling
   [ -n ] [ --noStun ] (type=FLAG)
          Do NOT use a stun server (overrides -s and -t).
   [ -s ] [ --stunServer ] (type=STRING, default=stun.l.google.com)
          STUN server URL or IP address.
   [ -t ] [ --stunPort ] (type=INTEGER, range=0...65535, default=19302)
          STUN server port.
   [ -w ] [ --webSocketServer ] (type=STRING, default=localhost)
          Web socket server URL or IP address.
   [ -x ] [ --webSocketPort ] (type=INTEGER, range=0...65535, default=8000)
          Web socket server port.
   [ -m ] [ --udpMux ] (type=FLAG)
          Use UDP multiplex.
   [ -u ] [ --turnServer ] (type=STRING)
          TURN server URL or IP address.
   [ -p ] [ --turnPort ] (type=INTEGER, range=0...65535, default=3478)
          TURN server port.
   [ -U ] [ --turnUser ] (type=STRING)
          TURN server username.
   [ -P ] [ --turnPass ] (type=STRING)
          TURN server password.
   [ -i ] [ --inputDevice ] (type=STRING, default)=
          Input video device.
   [ -a ] [ --audioDevice ] (type=STRING, default=none)
          Input audio device.
   [ -r ] [ --sampleRate ] (type=INTEGER, default=48000)
          Audio sample rate.
   [ -C ] [ --channels ] (type=INTEGER, default=1)
          Audio channels.
   [ -f ] [ --audioFormat ] (type=STRING, default=S16LE)
          Audio input_format.
   [ -V ] [ --videoFormat ] (type=STRING, default=mjpeg)
          Video input format.
   [ -c ] [ --client_id ] (type=STRING)
          Client identifier.
   [ -d ] [ --debug ] (type=FLAG)
          Enable debug output.
   [ -R ] [ --resolution ] (type=STRING, default=640x480)
          Video resolution in WIDTHxHEIGHT input_format.
   [ -F ] [ --framerate ] (type=INTEGER, range=1...120, default=30)
          Video encoding framerate.
   [ -h ] [ --help ] (type=FLAG)
          Display this help and exit.
```

#### 3.3 前端操作
编辑html替换信令服务器地址和STUN/TURN服务器地址

前往：[data_rtc.js](data_track/web/data_rtc.js)

```js
 const dataConfig = {
    iceServers: [{
      urls: 'stun:stun.l.google.com:19302', // change to your STUN server
    }],
  };


  const dataUrl = `ws://fy403.cn:8000/${dataLocalId}`;
```
前往：[video_rtc.js](data_track/web/video_rtc.js)
```js
    const url = `ws://fy403.cn:8000/${localId}`;
    const config = {
        iceServers: [{
                "urls": ["stun:stun.cloudflare.com:3478",
                    // "stun:stun.cloudflare.com:53"
                ]
            },
            {
                "urls": [
                    "turn:turn.cloudflare.com:3478?transport=udp",
                ],
                "username": "g0xxxxxxxxxxx",
                "credential": "95yyyyyyyyy"
            },
        ],
    };
```

浏览器打开[index.html](data_track/web/index.html)等待画面和信号连接正常后，先按`f`换挡(按一次F换挡一次)，一共五档可调：
```c++
// Throttle levels
const ThrottleLevel THROTTLE_LEVELS[] = {
    {0, 0, "Stop"},
    {1, 25, "Low speed"},
    {2, 50, "Medium speed"},
    {3, 75, "Fast"},
    {4, 100, "Full speed"}
};
```
通过`wsad`操作，`空格`急停。按`q`重启开发板的信号服务。

如果自定义了client_id，则需要修改此处。修改后点击Send可以重新建立P2P连接

<img src="README.assets\image-20251030141913114.png" alt="image-20251030141913114" style="zoom:80%;" />

## 技术细节

### 1.系统架构图
<img src="README.assets\image-20251030170418827.png" alt="image-20251030170418827" style="zoom:80%;" />
WebRTC（Web Real-Time Communication）是一种开源技术，支持浏览器、移动端和嵌入式设备之间的实时音视频和数据传输。其核心目标是通过点对点（P2P）连接实现低延迟通信，无需安装插件或中间媒介。可以参考[WebRTC连接原理](https://blog.csdn.net/yanceyxin/article/details/149752514)

在本案例中，将音视频数据通道和控制信号通道分离，分别在不同的RTP连接中传递数据。避免彼此相互干扰。使用WebRTC的主要目的在于：

- 它是一项提供低延迟、高质量实时音频和视频通信的技术。
- 它是一个全面的客户端多媒体框架，具有音频和视频处理能力，旨在跨平台兼容。
- 它是一组标准化的API（是W3C推荐的一部分），允许网页开发者创建多样化的实时音频和视频应用程序。

> [腾讯TRTC: WebRTC是如何工作的以及它的优缺点](https://trtc.io/zh/blog/details/what-is-webrtc))

### 2. 连接建立流程
WebRTC的连接建立分为以下几个阶段：

1. **候选地址**：通过ICE服务器获取网络地址IP，即ICE候选者信息。
2. **信令交换**：通过信令服务器（借助WebSocket）传递SDP（Session Description Protocol）和ICE候选者信息。
3. **媒体协商**：双方交换SDP，确定编解码器、分辨率等参数。
4. **网络穿透**：通过ICE信息尝试P2P（借助UDP）连接，若失败则回退到TURN中继流量。所有流量都经过TURN服务器中转。

### 3. 数据传输过程

当音视频RTP与数据RTP通道建立后，开发板将捕获的音视频数据进行编码。之后，借助4G网络将编码数据推送到桌面端。桌面端将键鼠操作封装为控制帧，同样借助4G网络传输到开发板。最后，开发板通过驱动程序将控制信号转换为控制命令，通过串口发送到电机驱动器。电机驱动器控制马达工作。

### 4.音视频工作过程

<img src="README.assets\image-20251030185412346.png" alt="image-20251030185412346" style="zoom:80%;" />

### 3.材料清单

|      名称       | 价格 |                    备注                     |
|:-------------:| :--: | :-----------------------------------------: |
| OrangepiZero2 | 125  | 建议买带GPU加速的开发板并附带【文档】！！！ |
|     四路驱动器     |  40  |   要留意是否可以稳定板载5V给开发板子供电    |
|      摄像头      |  40  |          最好买USB免驱的，直插直用          |
|    4G网络模块     |  20  |           最好是买支持RNDIS的模块           |
|     RC遥控车     | 160  |      选择自己喜欢的型号【要方便改造】       |
|    7.4V电池     |  10  |          记录选大容量【留意尺寸】           |
|    USB扩展线     |  12  |               尽量选模块化的                |
|     若干线材      |  20  |               准备一些杜邦线                |

<img src="README.assets\image-20251029212435996.png" alt="image-20251029212435996" style="zoom:83%;" />

> 尽量选择带GPU的开发板子，并且还需要获取板子安装驱动的文档。这样音视频编解码速度快，也能缓解CPU的压力。
>
> 代码里面是不带GPU加速，仍然是软编码(效果还可以)。板子是4H1G，正常工作CPU利用率平均30%，内存占用300MB，音视频码率1.8Mbps，视频延迟300ms左右。
><img src="README.assets\image-20251030135634832.png" alt="image-20251030135634832" style="zoom:80%;" />

<img src="README.assets\image-20251029180420095.png" alt="image-20251029180420095" style="zoom: 50%;" />

> 驱动板尽量选择带稳压BEC给出开发板供电，但是有些开发板功耗比较大。用这块驱动板可能带不起来。这时候可以买一个5V的电池单独给开发板供电。
>
> 这款开发板还支持编码电机，后续如果要升级也比较好。

<img src="README.assets\image-20251103205553057.png" alt="image-20251103205553057" style="zoom: 50%;" />

> 选择4G模块尽量选择支持RNDIS的，这样插上USB和供电就能直接识别为一个网络接口【免去配置】。供电7.4V，用5V供电会导致信号速率低。
>
> 摄像头可以用第一种单目的【优点：柔软，小型化，已安装，缺点：过热，容易烧毁，排线易断（目前基本没看见带保修的，慎重选择！！！）】，第二种是带保修一年，一样的价格，虽然大一些；但用料好，值得入手。
>
> 麦克风根据开发板来选择接口，优先选择带降噪的。

## 后续计划

- [ ] 控制端语音推送
- [ ] 被控端扬声器播放
- [ ] 编码器电机支持
- [ ] GPU加速音视频推流
- [ ] 支持ROS系统
- [ ] 三维重建
- [ ] AI模型接入
- [ ] 自主导航

## QQ群交流
<img src="README.assets\qrcode_1764133405428.jpg" alt="qrcode_1764133405428" style="zoom: 50%;" />
