# OV5647 Camera Configuration Tool

## 项目结构
```
/root/ov5647-config/
├── install.sh              # 一键安装脚本
├── uninstall.sh           # 卸载脚本
├── ov5647-config-tool.sh # 主配置工具（单文件）
├── ov5647.conf          # 默认配置文件
├── ov5647-config.service # systemd服务文件
└── README.md            # 使用说明
```

## 快速开始

### 1. 安装
```bash
cd /root/ov5647-config
sudo ./install.sh
```

### 2. 使用
```bash
# 列出所有支持的分辨率和帧率
sudo ov5647-config-tool.sh -l

# 配置为1080p @ 30fps（默认）
sudo ov5647-config-tool.sh -r 1920x1080

# 配置为720p @ 60fps
sudo ov5647-config-tool.sh -r 1280x720

# 配置为VGA @ 90fps
sudo ov5647-config-tool.sh -r 640x480

# 检查当前配置
sudo ov5647-config-tool.sh -c

# 测试实际帧率
sudo ov5647-config-tool.sh -t
```

### 3. 卸载
```bash
cd /root/ov5647-config
sudo ./uninstall.sh
```

## 问题说明

**问题**：OV5647摄像头输出被固定在15fps，即使降低分辨率也无法提高。

**原因**：OV5647驱动默认配置为2592x1944 @ 15fps。ISP缩放不会改变传感器输出帧率。

**解决**：直接配置传感器子设备到目标分辨率，从而获得正确的帧率。

## 支持的分辨率

| 分辨率 | 最大帧率 | 说明 |
|--------|----------|------|
| 2592x1944 | 15 fps | 5百万像素（全分辨率）|
| 1920x1080 | 30 fps | 1080p 全高清（默认）|
| 1280x720  | 60 fps | 720p 高清 |
| 640x480   | 90 fps | VGA |

## 安装位置

安装后，文件会被复制到：
- 主脚本：`/usr/local/bin/ov5647-config-tool.sh`
- 配置文件：`/etc/default/ov5647`
- 服务文件：`/etc/systemd/system/ov5647-config.service`
- 文档：`/usr/local/share/doc/ov5647-config/`

## 使用 ffmpeg 捕获视频

```bash
# 1080p @ 30fps
ffmpeg -f v4l2 -framerate 30 -video_size 1920x1080 -i /dev/video0 output.mp4

# 720p @ 60fps
ffmpeg -f v4l2 -framerate 60 -video_size 1280x720 -i /dev/video0 output.mp4

# VGA @ 90fps
ffmpeg -f v4l2 -framerate 90 -video_size 640x480 -i /dev/video0 output.mp4
```

## 服务管理

```bash
# 检查服务状态
systemctl status ov5647-config.service

# 启动服务
systemctl start ov5647-config.service

# 停止服务
systemctl stop ov5647-config.service

# 重启服务
systemctl restart ov5647-config.service

# 查看日志
journalctl -u ov5647-config.service -f
```

## 技术细节

### Media Controller 管线
```
OV5647 传感器 (m00_b_ov5647) 
    ↓ (MIPI CSI-2, 2-lane)
rockchip-csi2-dphy0
    ↓
rkisp-csi-subdev
    ↓
rkisp-isp-subdev
    ↓
rkisp_mainpath (/dev/video0)
```

### 关键配置命令
```bash
# 配置传感器分辨率（提高帧率的关键）
v4l2-ctl -d /dev/v4l-subdev3 --set-subdev-fmt width=1920,height=1080

# 验证配置
media-ctl -d /dev/media0 --get-v4l2 '"m00_b_ov5647 2-0036":0'
```

### 帧间隔格式
在 media-ctl 输出中：`@10000/300000` 表示：
- 帧间隔 = 10000 / 300000 = 1/30 秒
- 帧率 = 30 fps

## 故障排除

### 摄像头忙（设备或服务忙）
rkaiq_3A 服务可能正在使用摄像头。停止它：
```bash
systemctl stop rkaiq_3A.service
```

### 配置后帧率仍然是 15fps
检查配置是否应用：
```bash
ov5647-config-tool.sh -c
```

验证传感器配置：
```bash
media-ctl -d /dev/media0 --get-v4l2 '"m00_b_ov5647 2-0036":0'
```

应该显示：`fmt:SGBRG10_1X10/1920x1080@10000/300000`（对于 30fps）

### 低光照下帧率低
更高的帧率有更短的曝光时间。改善光照或使用手动曝光：
```bash
v4l2-ctl -d /dev/video0 --set-ctrl=exposure=500
```

## 作者
由 CodeBuddy Assistant 生成
日期：2026-05-24
版本：2.0
