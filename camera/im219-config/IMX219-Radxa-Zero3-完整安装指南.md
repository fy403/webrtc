# IMX219 树莓派摄像头在 Radxa Zero 3 上的完整安装指南

> **适用硬件**: Radxa Zero 3 / Zero 3W (RK3566)  
> **摄像头**: 树莓派 Camera v2 (Sony IMX219, 800万像素)  
> **系统**: Debian GNU/Linux, Kernel 6.1.84-10-rk2410-nocsf  
> **日期**: 2026-05-27

---

## 目录

1. [前置环境检查](#1-前置环境检查)
2. [编写设备树 Overlay (DTS)](#2-编写设备树-overlay-dts)
3. [编译 DTS → DTBO](#3-编译-dts--dtbo)
4. [通过 rsetup 安装 Overlay](#4-通过-rsetup-安装-overlay)
5. [配置 rkaiq 3A 引擎](#5-配置-rkaiq-3a-引擎)
6. [创建一键捕获脚本](#6-创建一键捕获脚本)
   - 6.4 [为什么不能直接用 ffmpeg 捕获（重要）](#64-为什么不能直接用-ffmpeg-捕获重要)
7. [验证摄像头](#7-验证摄像头)
8. [故障排查](#8-故障排查)

---

## 1. 前置环境检查

### 1.1 确认系统信息

```bash
# 查看设备型号
cat /proc/device-tree/compatible
# radxa,zero3w-aic8800ds2 radxa,zero3 rockchip,rk3566

# 查看内核版本
uname -r
# 6.1.84-10-rk2410-nocsf
```

### 1.2 确认必要的工具已安装

```bash
# 设备树编译器
apt install -y device-tree-compiler

# V4L2 工具（调试用）
apt install -y v4l-utils

# i2c 工具（调试用）
apt install -y i2c-tools

# ffmpeg（图片转换用）
apt install -y ffmpeg
```

### 1.3 确认内核头文件存在

```bash
ls /usr/src/linux-headers-6.1.84-10-rk2410-nocsf/include/dt-bindings/
# 应该能看到 gpio/ pinctrl/ 等目录
```

### 1.4 查看官方的 Overlay 示例

```bash
# 如果安装了 radxa-overlays 包
ls /usr/src/radxa-overlays-*/arch/arm64/boot/dts/rockchip/overlays/*zero3*camera*
# 例如: radxa-zero3-rpi-camera-v2.dts (官方 IMX219 overlay)
```

---

## 2. 编写设备树 Overlay (DTS)

### 2.1 DTS 文件完整内容

创建文件 `/root/radxa-zero3-rpi-camera-v2-imx219.dts`：

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/pinctrl/rockchip.h>

/ {
	metadata {
		title = "Enable Raspberry Pi Camera v2 (IMX219)";
		compatible = "radxa,zero3";
		category = "camera";
		exclusive = "csi2_dphy0";
		description = "Enable Raspberry Pi Camera v2 with IMX219 sensor on Radxa Zero 3.";
	};
};

/* 24MHz 外部时钟 */
&{/} {
	clk_cam_24m: external-camera-clock-24m {
		status = "okay";
		compatible = "fixed-clock";
		clock-frequency = <24000000>;
		clock-output-names = "clk_cam_24m";
		#clock-cells = <0>;
	};

	/* 摄像头电源使能 GPIO (GPIO3_PC6 = GPIO3_22) */
	camera_pwdn_gpio: camera-pwdn-gpio {
		status = "okay";
		compatible = "regulator-fixed";
		regulator-name = "camera_pwdn_gpio";
		regulator-always-on;
		regulator-boot-on;
		enable-active-high;
		gpio = <&gpio3 RK_PC6 GPIO_ACTIVE_HIGH>;
	};
};

/* I2C2 总线 - IMX219 传感器通信 */
&i2c2 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&i2c2m1_xfer>;
	#address-cells = <1>;
	#size-cells = <0>;

	camera_imx219: camera-imx219@10 {
		status = "okay";
		compatible = "sony,imx219";
		reg = <0x10>;                          /* I2C 地址 */
		clocks = <&clk_cam_24m>;
		clock-names = "xvclk";
		rockchip,camera-module-index = <0>;
		rockchip,camera-module-facing = "back";
		rockchip,camera-module-name = "rpi-camera-v2";
		rockchip,camera-module-lens-name = "default";

		port {
			ucam_out0: endpoint {
				remote-endpoint = <&mipi_in_ucam0>;
				data-lanes = <1 2>;
			};
		};
	};
};

/* CSI D-PHY 硬件控制器 */
&csi2_dphy_hw {
	status = "okay";
};

/* CSI D-PHY0 数据通道 */
&csi2_dphy0 {
	status = "okay";

	ports {
		#address-cells = <1>;
		#size-cells = <0>;

		port@0 {
			reg = <0>;
			#address-cells = <1>;
			#size-cells = <0>;

			mipi_in_ucam0: endpoint@1 {
				reg = <1>;
				remote-endpoint = <&ucam_out0>;
				data-lanes = <1 2>;
			};
		};

		port@1 {
			reg = <1>;
			#address-cells = <1>;
			#size-cells = <0>;

			dphy0_out: endpoint@1 {
				reg = <1>;
				remote-endpoint = <&isp0_in>;
			};
		};
	};
};

/* RKISP 虚拟通道 0 */
&rkisp_vir0 {
	status = "okay";

	port {
		#address-cells = <1>;
		#size-cells = <0>;

		isp0_in: endpoint@0 {
			reg = <0>;
			remote-endpoint = <&dphy0_out>;
		};
	};
};

/* RKISP 主设备 */
&rkisp {
	status = "okay";
};

/* RKISP MMU */
&rkisp_mmu {
	status = "okay";
};

/* RKCIF MMU */
&rkcif_mmu {
	status = "okay";
};

/* RKCIF */
&rkcif {
	status = "okay";
};
```

### 2.2 DTS 关键点说明

| 项目 | 说明 |
|------|------|
| `metadata.compatible` | 必须为 `"radxa,zero3"`，rsetup 据此匹配设备 |
| `metadata.exclusive` | `"csi2_dphy0"` 防止与其他摄像头 overlay 冲突 |
| `clk_cam_24m` | **必须有 label**，否则 `&clk_cam_24m` 引用无法解析 |
| `reg = <0x10>` | IMX219 的 I2C 地址 |
| `camera-module-name` | 必须与 `/etc/iqfiles/` 中的 IQ 文件名对应 |

### 2.3 数据链路图

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  IMX219      │    │ CSI2 D-PHY0  │    │  RKISP       │    │ /dev/video0  │
│  Sensor      │───▶│ 接收 MIPI    │───▶│  图像信号     │───▶│  NV12/YUV    │
│  I2C: 0x10   │    │ 数据         │    │  处理器       │    │  输出节点    │
│  24MHz XCLK  │    │              │    │              │    │  mainpath    │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

---

## 3. 编译 DTS → DTBO

### 3.1 编译命令

```bash
# 使用 C 预处理器展开 #include，然后 dtc 编译为二进制
cpp -nostdinc \
    -I /usr/src/linux-headers-6.1.84-10-rk2410-nocsf/include \
    -undef -x assembler-with-cpp \
    /root/radxa-zero3-rpi-camera-v2-imx219.dts \
    | dtc -I dts -O dtb -b 0 \
    -o /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo -
```

### 3.2 验证编译结果

```bash
# 检查文件是否生成
ls -la /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo
# -rw-r--r-- 1 root root 3438 May 27 13:46 radxa-zero3-rpi-camera-v2-imx219.dtbo

# 反编译检查 phandle 是否正确生成
dtc -I dtb -O dts /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo 2>/dev/null | head -40
# 应该看到 external-camera-clock-24m 节点中有 phandle = <0x01>
```

### 3.3 常见编译错误

| 错误 | 原因 | 解决方法 |
|------|------|----------|
| `label 'clk_cam_24m' not found` | DTS 中时钟节点缺少 label | 确保写成 `clk_cam_24m: external-camera-clock-24m` |
| `undefined reference 'RK_PC6'` | 缺少 include 头文件 | 添加 `#include <dt-bindings/gpio/gpio.h>` |
| `unknown include path` | 头文件路径不对 | 检查 `-I` 参数指向内核头文件目录 |

---

## 4. 通过 rsetup 安装 Overlay

### 4.1 前提：理解 u-boot overlay 管理机制

Radxa Zero 3 使用 `u-boot-update` 管理启动配置。Overlay 的管理方式分为两层：

```
/etc/default/u-boot          ← 全局配置 (U_BOOT_FDT_OVERLAYS_DIR)
        │
        ▼
/boot/dtbo/*.dtbo            ← 已启用的 overlay
/boot/dtbo/*.dtbo.disabled   ← 已禁用的 overlay
        │
        ▼
u-boot-update 自动扫描
        │
        ▼
/boot/extlinux/extlinux.conf ← 最终启动配置 (由 u-boot-update 自动生成)
```

**关键规则**：
- **绝对不能**在 `/etc/default/u-boot` 中手动设置 `U_BOOT_FDT_OVERLAYS` 变量
- 否则 rsetup 会检测到并禁用其 overlay 管理功能
- Overlay 的启用/禁用通过文件重命名完成（`.dtbo.disabled` ↔ `.dtbo`）

### 4.2 步骤一：清理冲突配置

```bash
# 编辑 /etc/default/u-boot，确保 U_BOOT_FDT_OVERLAYS 被注释或删除
sed -i 's/^U_BOOT_FDT_OVERLAYS=/#U_BOOT_FDT_OVERLAYS=/' /etc/default/u-boot
```

### 4.3 步骤二：清理冗余的 overlay

```bash
# 只保留需要的 overlay 处于启用状态
# IMX219 overlay 已经包含了 I2C2 使能，不需要单独的 I2C2 overlay

# 禁用不需要的 overlay
mv /boot/dtbo/enable-i2c2-only.dtbo /boot/dtbo/enable-i2c2-only.dtbo.disabled 2>/dev/null

# 清理之前可能残留的重复文件
rm -f /boot/dtbo/enabled-enable-i2c2-only.dtbo 2>/dev/null
rm -f /boot/dtbo/enabled-enable-i2c2-only.dtbo.disabled 2>/dev/null

# 清除旧的 disabled 副本（如果存在）
rm -f /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo.disabled 2>/dev/null

# 检查最终状态：应该只有 2 个 .dtbo 文件
ls -la /boot/dtbo/*.dtbo
# radxa-zero3-external-antenna.dtbo         ← 外部天线
# radxa-zero3-rpi-camera-v2-imx219.dtbo     ← IMX219 摄像头
```

### 4.4 步骤三：重新生成启动配置

```bash
u-boot-update
# 输出:
#   P: Checking for EXTLINUX directory... found.
#   P: Writing config for vmlinuz-6.1.84-10-rk2410-nocsf...
#   P: Updating /boot/extlinux/extlinux.conf...
```

### 4.5 步骤四：验证启动配置

```bash
cat /boot/extlinux/extlinux.conf
```

应该看到 `fdtoverlays` 行包含 IMX219 overlay：

```
label l0
	menu label Debian GNU/Linux 6.1.84-10-rk2410-nocsf
	linux /boot/vmlinuz-6.1.84-10-rk2410-nocsf
	initrd /boot/initrd.img-6.1.84-10-rk2410-nocsf
	fdtdir /usr/lib/linux-image-6.1.84-10-rk2410-nocsf/
	fdtoverlays  /boot/dtbo/radxa-zero3-external-antenna.dtbo /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo
	append root=UUID=... ro quiet
```

### 4.6 通过 rsetup TUI 管理（可选）

```bash
sudo rsetup
```

在 TUI 界面中：
1. 选择 **"Overlays"**
2. 进入 **"camera"** 分类
3. 能看到 **"Enable Raspberry Pi Camera v2 (IMX219)"** 条目
4. 按空格键启用/禁用

### 4.7 步骤五：重启使 Overlay 生效

```bash
reboot
```

---

## 5. 配置 rkaiq 3A 引擎

> **重要**：Rockchip ISP 需要 `rkaiq_3A_server` 持续运行才能输出图像帧。没有它，ISP 不会开始帧传输。

### 5.1 验证 IQ 文件存在

```bash
ls /etc/iqfiles/imx219_rpi-camera-v2_default.json
```

IQ 文件命名规则：`{传感器名}_{模块名}_default.json`
- 传感器名：`imx219`
- 模块名：来自 DTS 中 `rockchip,camera-module-name = "rpi-camera-v2"`

### 5.2 确保服务开机自启

```bash
sudo systemctl enable rkaiq_3A.service
sudo systemctl start rkaiq_3A.service
```

### 5.3 验证服务运行状态

```bash
sudo systemctl status rkaiq_3A.service
# 应该显示 Active: active (running)
```

期望的日志关键词（通过 `journalctl -u rkaiq_3A.service` 查看）：

```
XCORE:K:cid[0] rk_aiq_uapi_sysctl_init success. iq:/etc/iqfiles//imx219_rpi-camera-v2_default.json
XCORE:K:cid[0] rk_aiq_uapi_sysctl_prepare success. mode:0
DBG: subscribe events from /dev/video9 success !
DBG: /dev/media0: wait stream start event...
```

> 注意：`ERR: Bad media topology for: /dev/mediaX` 是正常的，因为系统中可能存在其他不相关的 media 设备。

---

## 6. 创建一键捕获脚本

### 6.1 脚本文件

创建 `/root/capture.sh`：

```bash
#!/bin/bash
# IMX219 Camera Capture Script for Radxa Zero 3
# Usage: ./capture.sh [output.jpg] [width] [height] [frames]

set -e

OUTPUT="${1:-/tmp/camera_capture.jpg}"
WIDTH="${2:-1920}"
HEIGHT="${3:-1080}"
COUNT="${4:-1}"
TMP_YUV="/tmp/camera_tmp.yuv"

echo "=========================================="
echo "IMX219 Camera Capture"
echo "=========================================="
echo "  Resolution: ${WIDTH}x${HEIGHT}"
echo "  Output:     ${OUTPUT}"
echo "  Frames:     ${COUNT}"
echo "=========================================="
echo ""

# 检查 video0 是否存在
if [ ! -e /dev/video0 ]; then
    echo "[ERROR] /dev/video0 not found! Check if overlay is enabled."
    exit 1
fi

# 确保 rkaiq 运行中
if ! pgrep -x rkaiq_3A_server > /dev/null; then
    echo "[WARN] rkaiq_3A_server not running, starting..."
    sudo systemctl start rkaiq_3A.service
    sleep 2
fi

# 步骤1: 从 ISP 捕获原始 YUV 帧
echo "[1/2] Capturing ${COUNT} frame(s) from /dev/video0..."
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=${WIDTH},height=${HEIGHT},pixelformat=NV12 \
    --stream-mmap=3 \
    --stream-count=${COUNT} \
    --stream-to="${TMP_YUV}"

FILE_SIZE=$(stat -c%s "${TMP_YUV}" 2>/dev/null || echo 0)
echo "       Captured ${FILE_SIZE} bytes"

if [ "$FILE_SIZE" -eq 0 ]; then
    echo "[ERROR] Capture failed! Output file is empty."
    exit 1
fi

# 步骤2: YUV(RAW) → JPEG
echo "[2/2] Converting YUV to JPEG..."
ffmpeg -y \
    -f rawvideo -pix_fmt nv12 -s ${WIDTH}x${HEIGHT} \
    -i "${TMP_YUV}" \
    -frames:v 1 -q:v 2 -update 1 \
    "${OUTPUT}" \
    2>/dev/null

rm -f "${TMP_YUV}"

if [ -f "${OUTPUT}" ]; then
    echo ""
    echo "=========================================="
    echo "  SUCCESS! Image saved to: ${OUTPUT}"
    echo "  Size: $(stat -c%s "${OUTPUT}") bytes"
    echo "=========================================="
else
    echo "[ERROR] JPEG conversion failed!"
    exit 1
fi
```

### 6.2 赋予执行权限

```bash
chmod +x /root/capture.sh
```

### 6.3 使用方式

```bash
# 默认 1920×1080，输出到 /tmp/camera_capture.jpg
./capture.sh

# 指定输出路径
./capture.sh /home/user/my_photo.jpg

# 全分辨率 3280×2464
./capture.sh /tmp/8mp.jpg 3280 2464

# 捕获 5 帧
./capture.sh /tmp/frames.jpg 1920 1080 5
```

### 6.4 为什么不能直接用 ffmpeg 捕获（重要）

当你尝试直接用 ffmpeg 从 `/dev/video0` 捕获时：

```bash
root@radxa-zero3:~# ffmpeg -f v4l2 -i /dev/video0 -frames 1 test.jpg
```

你会得到如下错误：

```
[video4linux2,v4l2 @ 0xaaab0543e360] Not a video capture device.
/dev/video0: No such device
```

但在两秒后用 `./capture.sh 1.jpg` 却成功输出：

```
[1/2] Capturing 1 frame(s) from /dev/video0...
       Captured 3110400 bytes
[2/2] Converting YUV to JPEG...
  SUCCESS! Image saved to: 1.jpg
  Size: 345549 bytes
```

#### 根本原因分析

这不是 bug，而是 **RK ISP 驱动架构的固有特性**，涉及三层原因：

**第一层：V4L2 Multiplanar API**

RK ISP (`rkisp_v5` 驱动) 的视频节点使用 V4L2 **Multiplanar**（多平面）API 而非传统的单平面 API：

```bash
root@radxa-zero3:~# v4l2-ctl -d /dev/video0 --info
Driver Info:
	Driver name      : rkisp_v5
	Card type        : rkisp_mainpath
	Capabilities     : 0x84201000
		Video Capture Multiplanar    ← 关键：Multiplanar
		Streaming
		Extended Pix Format
```

Multiplanar API 中，视频帧的各个平面（如 Y 平面、UV 平面）存放在**不连续的内存区域**，通过 `struct v4l2_plane` 数组描述。ffmpeg 的 V4L2 输入模块 (`libavdevice/v4l2.c`) 对 Multiplanar 类型的设备仅做探测（probe），并**不会真正打开和读取**，直接返回 `Not a video capture device`。

| 对比 | 传统 V4L2 设备 (USB 摄像头) | RK ISP (Multiplanar) |
|------|---------------------------|----------------------|
| 内存模型 | 连续单缓冲区 | 多平面分散缓冲区 |
| V4L2 type | `VIDEO_CAPTURE` | `VIDEO_CAPTURE_MPLANE` |
| ffmpeg 支持 | ✅ 直接可读 | ❌ 拒绝打开 |
| v4l2-ctl 支持 | ✅ | ✅ 原生支持 |

**第二层：RKISP 的流启动依赖 `rkaiq_3A_server`**

即使绕过 Multiplanar 问题，RK ISP 还有一个特殊机制：ISP 硬件的帧输出**必须由 3A 引擎触发**。`rkaiq_3A_server` 通过 `/dev/video9`（`rkisp-input-params`）向 ISP 写入 3A 参数（曝光、白平衡、增益等），并通过 `/dev/video8`（`rkisp-statistics`）读取统计信息形成闭环。只有这个闭环建立后，ISP 才会开始输出帧。

```
rkaiq_3A_server
    │
    │ 写入 3A 参数
    ▼
┌──────────────┐     ┌──────────────┐
│ /dev/video9  │────▶│ rkisp-isp    │
│ input-params │     │ -subdev      │
└──────────────┘     └──────┬───────┘
                            │ 输出帧
                     ┌──────▼───────┐     ┌──────────────┐
                     │ rkisp        │────▶│ /dev/video0  │
                     │ mainpath     │     │ (NV12 帧)   │
                     └──────────────┘     └──────────────┘
                            │
                     ┌──────▼───────┐
                     │ /dev/video8  │ 读取统计信息（反馈闭环）
                     │ statistics   │
                     └──────────────┘
                            ▲
                            │
                      rkaiq_3A_server (闭环)
```

**第三层：NV12 格式需要 ISP 后处理**

IMX219 传感器输出的是 10 位 RAW Bayer 格式（SRGGB10），不能直接被 ffmpeg 解码为 JPEG。必须经过 ISP 做 demosaicing、颜色校正、降噪等处理后转换为 NV12（YUV 4:2:0）。这个处理链路也需要 rkaiq 初始化后才能正常工作。

#### 正确的捕获方案

```bash
# ❌ 错误：直接 ffmpeg 捕获 → "Not a video capture device"
ffmpeg -f v4l2 -i /dev/video0 -frames 1 test.jpg

# ✅ 正确：两步法
# 步骤1：v4l2-ctl 捕获原始 NV12/YUV 帧
v4l2-ctl -d /dev/video0 \
    --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
    --stream-mmap=3 --stream-count=1 --stream-to=/tmp/frame.yuv

# 步骤2：ffmpeg 将 YUV 转换为 JPEG
ffmpeg -y -f rawvideo -pix_fmt nv12 -s 1920x1080 \
    -i /tmp/frame.yuv -frames:v 1 test.jpg
```

`capture.sh` 脚本封装了这两个步骤，使用时只需一行命令即可获得 JPEG 图片。

#### 技术总结

| 失败原因 | 技术细节 |
|----------|----------|
| Multiplanar API | RK ISP v5 强制使用 `V4L2_CAP_VIDEO_CAPTURE_MPLANE`，ffmpeg 的 v4l2 模块只支持单平面 |
| 3A 依赖 | ISP 帧输出依赖 rkaiq 的参数-统计闭环，单独打开 video0 不会产生帧 |
| 格式链 | 传感器 RAW Bayer → ISP NV12 → JPEG，ffmpeg 无法跳过 ISP 阶段直接读取传感器 |

这不是驱动 bug，而是 Rockchip ISP 架构的设计选择：将复杂的 ISP 管线抽象为 media controller 拓扑 + Multiplanar 视频节点，需要配套的 userspace 3A 服务配合使用。

---

## 7. 验证摄像头

### 7.1 重启后自动诊断

```bash
#!/bin/bash
echo "===== IMX219 Camera Diagnostic ====="

echo ""
echo "[1] Overlay status:"
grep fdtoverlays /boot/extlinux/extlinux.conf

echo ""
echo "[2] I2C device scan (bus 2):"
i2cdetect -y 2
# 0x10 位置应该显示 "UU" (被内核驱动占用)

echo ""
echo "[3] IMX219 kernel messages:"
dmesg | grep -i imx219 | tail -5
# 应该看到: Model ID 0x0219

echo ""
echo "[4] Media pipeline:"
media-ctl -p -d /dev/media0 2>/dev/null | grep -E "entity|ENABLED|imx219"

echo ""
echo "[5] V4L2 devices:"
v4l2-ctl --list-devices

echo ""
echo "[6] rkaiq service:"
systemctl is-active rkaiq_3A.service

echo ""
echo "[7] Test capture:"
./capture.sh /tmp/diagnostic_test.jpg 640 480
```

### 7.2 期望的验证结果

| 检查项 | 期望结果 |
|--------|----------|
| extlinux.conf | `fdtoverlays ... radxa-zero3-rpi-camera-v2-imx219.dtbo` |
| I2C 0x10 地址 | `UU` (内核驱动已占用) |
| dmesg | `imx219 2-0010: Model ID 0x0219` |
| media controller | `m00_b_imx219` → `rockchip-csi2-dphy0` → `rkisp-isp-subdev` → `rkisp_mainpath` 全链路 `[ENABLED]` |
| rkaiq | `active` |
| 捕获测试 | 成功输出 JPEG 文件 |

---

## 8. 故障排查

### 8.1 摄像头未识别

```bash
# 检查 I2C 总线上是否能看到设备
i2cdetect -y 2
```

| 现象 | 原因 | 解决方法 |
|------|------|----------|
| 全显示 `--` | I2C2 未启用或摄像头未连接 | 检查物理连接，确认 overlay 已正确加载 |
| `10` 显示（非 UU） | 设备存在但驱动未加载 | 检查 overlay 中 I2C 地址和 compatible 字符串 |
| `UU` 显示 | 正常，驱动已绑定 | 继续下一步检查 |

### 8.2 CSI D-PHY 未探测

```bash
dmesg | grep -i "csi2-dphy"
```

如果没有 `csi2 dphy hw probe successfully`：
- 检查 overlay 是否在 extlinux.conf 中
- 检查是否有其他 overlay 冲突（`exclusive = "csi2_dphy0"`）

### 8.3 rkaiq 启动失败

```bash
sudo journalctl -u rkaiq_3A.service --no-pager | tail -20
```

常见问题：
- **IQ 文件缺失**：确保 `/etc/iqfiles/imx219_rpi-camera-v2_default.json` 存在
- **模块名不匹配**：检查 DTS 中 `camera-module-name` 与 IQ 文件名一致

### 8.4 捕获无输出/超时

```bash
# 确认 3A 服务正在运行
ps aux | grep rkaiq_3A_server
```

如果 rkaiq 未运行 → `sudo systemctl start rkaiq_3A.service`

### 8.5 rsetup 提示 "overlay feature is temporarily disabled"

这是因为 `/etc/default/u-boot` 中手动设置了 `U_BOOT_FDT_OVERLAYS`。

**解决方法**：
```bash
sudo sed -i 's/^U_BOOT_FDT_OVERLAYS=/#U_BOOT_FDT_OVERLAYS=/' /etc/default/u-boot
sudo u-boot-update
```

### 8.6 系统无法启动

如果添加 overlay 后系统无法启动：

1. 重启，在 U-Boot 菜单选择 **rescue target** 启动项
2. 登录后移除 overlay：
   ```bash
   sudo mv /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo \
           /boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo.disabled
   sudo u-boot-update
   sudo reboot
   ```

---

## 附录 A: 关键文件清单

| 文件路径 | 说明 |
|----------|------|
| `/root/radxa-zero3-rpi-camera-v2-imx219.dts` | IMX219 DTS 源文件 |
| `/boot/dtbo/radxa-zero3-rpi-camera-v2-imx219.dtbo` | 编译后的 overlay |
| `/boot/extlinux/extlinux.conf` | U-Boot 启动配置（由 u-boot-update 自动生成） |
| `/etc/default/u-boot` | U-Boot 全局配置 |
| `/etc/iqfiles/imx219_rpi-camera-v2_default.json` | ISP 3A 调校参数 |
| `/lib/systemd/system/rkaiq_3A.service` | rkaiq 系统服务 |
| `/root/capture.sh` | 一键捕获脚本 |

## 附录 B: 常用调试命令

```bash
# 查看完整设备树
dtc -I fs /proc/device-tree 2>/dev/null | less

# 查看 CSI/ISP 节点状态
for p in /proc/device-tree/csi2-dphy-hw*/status \
         /proc/device-tree/csi2-dphy0/status \
         /proc/device-tree/rkisp-vir0/status; do
    echo -n "$p: "; cat "$p" 2>/dev/null || echo "NOT FOUND"
done

# 查看 media 控制器完整拓扑
media-ctl -p -d /dev/media0

# 查看视频设备能力
v4l2-ctl -d /dev/video0 --all

# 查看摄像头子设备信息
v4l2-ctl -d /dev/v4l-subdev3 --all
```

---

> **提示**：所有修改请在重启前仔细检查。建议在 U-Boot 中添加 rescue target 启动项以防万一（默认已存在）。
