#!/bin/bash
# =============================================================================
# IMX219 Pipeline Configuration Tool for Rockchip ISP (Radxa Zero 3 / RK3566)
# =============================================================================
# 注意：Rockchip BSP 内核 IMX219 驱动仅支持 4 种传感器模式，最大 30fps。
#       VBLANK 只读，无法通过调低消隐区来提升帧率。
#
# Usage:
#   ./imx219-config-tool.sh status         查看当前管线状态
#   ./imx219-config-tool.sh set 1080p      设置 1920x1080 @ 30fps + 条纹修复 (推荐)
#   ./imx219-config-tool.sh set medium     设置 1640x1232 @ 30fps + 条纹修复
#   ./imx219-config-tool.sh set full       设置 3280x2464 @ 21fps + 条纹修复 (拍照用)
#   ./imx219-config-tool.sh set 480p       设置 640x480   @ 30fps + 条纹修复
#   ./imx219-config-tool.sh set 1080p nofix 设置分辨率但不修复条纹
#   ./imx219-config-tool.sh test [count]   测试实际帧率 (默认60帧)
#   ./imx219-config-tool.sh init           (系统启动调用) 等待rkaiq后初始化
#   ./imx219-config-tool.sh banding fix    单独修复水平条纹
#   ./imx219-config-tool.sh banding revert 恢复原始 IQ 配置
#   ./imx219-config-tool.sh banding status 查看条纹修复状态
# =============================================================================

set -e

MEDIA_DEV="/dev/media0"
VIDEO_DEV="/dev/video0"

# Entity names
SENSOR="m00_b_imx219 2-0010"
DPHY="rockchip-csi2-dphy0"
CSI_SUBDEV="rkisp-csi-subdev"
ISP_SUBDEV="rkisp-isp-subdev"

# MBUS: SRGGB10_1X10 = 0x300f
MBUS_FMT="SRGGB10_1X10"

# IQ file paths
IQ_FILE="/etc/iqfiles/imx219_RADXA-CAMERA-8M_default.json"
IQ_BAK="/etc/iqfiles/imx219_RADXA-CAMERA-8M_default.json.bak"

# =============================================================================
# Helpers
# =============================================================================

die()   { echo "[ERROR] $*" >&2; exit 1; }
info()  { echo "[INFO]  $*"; }
warn()  { echo "[WARN]  $*"; }

# Find sensor subdev node (try auto-detect, fallback to fixed)
SENSOR_SUBDEV=""
find_sensor_subdev() {
    [ -n "$SENSOR_SUBDEV" ] && return
    for dev in /dev/v4l-subdev*; do
        if v4l2-ctl -d "$dev" --info 2>/dev/null | grep -qi imx219; then
            SENSOR_SUBDEV="$dev"
            return
        fi
    done
    SENSOR_SUBDEV="/dev/v4l-subdev3"
}

# =============================================================================
# Sensor mode table (BSP kernel IMX219 v00.01.01 — VBLANK 只读)
#
# 请求分辨率 → 传感器实际模式 → 帧率
# 3280x2464  → 3280x2464 (全尺寸)   → 21fps
# 1920x1080  → 1920x1080 (裁剪)     → 30fps ✓ 推荐
# 1640x1232  → 1640x1232 (2x2合并)  → 30fps
# 1280x720   → 1640x1232 (就近匹配) → 30fps
# 640x480    → 640x480  (裁剪)      → 30fps
# =============================================================================

# Get the actual sensor resolution that will result from a requested size
get_actual_sensor_mode() {
    case "$1" in
        3280x2464|full|max)   echo "3280x2464 21" ;;
        1920x1080|1080p|1080) echo "1920x1080 30" ;;
        1640x1232|medium)     echo "1640x1232 30" ;;
        1280x720|720p|720)    echo "1640x1232 30" ;;  # 驱动就近匹配到 1640x1232
        640x480|480p)         echo "640x480 30" ;;
        *)                    echo "1920x1080 30" ;;   # 默认回退到 1080p
    esac
}

# Parse resolution → width/height for media-ctl
parse_resolution() {
    case "$1" in
        1080p|1920x1080|1080) echo "1920 1080" ;;
        720p|1280x720|720)    echo "1280 720" ;;
        full|3280x2464|max)   echo "3280 2464" ;;
        1640x1232|medium)     echo "1640 1232" ;;
        640x480|480p)         echo "640 480" ;;
        *) die "未知模式: $1 (支持: 1080p | medium | full | 480p | 720p)" ;;
    esac
}

# =============================================================================
# Banding fix: patch rkaiq IQ file to limit analog gain (消除水平条纹)
# =============================================================================
# IMX219 在高模拟增益(>4x)时会产生明显的行噪声水平条纹。
# 通过限制模拟增益上限 + 优化 AE 策略来修复。

patch_iq_for_banding() {
    info "修复水平条纹: 限制模拟增益 ≤ 4x ..."

    # Backup original IQ file only once (first run)
    if [ ! -f "$IQ_BAK" ]; then
        cp "$IQ_FILE" "$IQ_BAK"
        info "  原始 IQ 已备份: $IQ_BAK"
    fi

    # 先校验当前 JSON 是否合法，损坏则自动从备份恢复
    if ! python3 -c "import json; json.load(open('$IQ_FILE'))" 2>/dev/null; then
        warn "  IQ 文件已损坏，从备份恢复..."
        if [ -f "$IQ_BAK" ]; then
            cp "$IQ_BAK" "$IQ_FILE"
            info "  ✓ 已从备份恢复"
        else
            warn "  ✗ 无可用备份，跳过修复"
            return 1
        fi
    fi

    python3 << 'PYEOF'
import json, os

IQ = '/etc/iqfiles/imx219_RADXA-CAMERA-8M_default.json'

with open(IQ, 'r') as f:
    data = json.load(f)

sc = data['sensor_calib']

# 核心修复: 限制模拟增益 ≤ 2x (原 11x，太阳下必须有上限)
sc['CISGainSet']['CISAgainRange']['Max'] = 2
sc['Gain2Reg']['GainRange'] = [1, 67, 256, 0, 1, 256, 43663]
sc['CISMinFps'] = 20

# AE route: 强光下压缩曝光时间，限制增益
mc = data['main_scene'][0]['sub_scene'][0]['scene_isp21']
la = mc['ae_calib']['LinearAeCtrl']
la['Route'] = {
    "TimeDot": [0, 0.001, 0.002, 0.004, 0.007, 0.01],
    "TimeDot_len": 6,
    "GainDot": [1, 1, 1.2, 1.5, 1.7, 2],
    "GainDot_len": 6,
    "IspDGainDot": [1, 1, 1, 1, 1, 1],
    "IspDGainDot_len": 6,
    "PIrisDot": [512, 512, 512, 512, 512, 512],
    "PIrisDot_len": 6
}
la['StrategyMode'] = 'AECV2_STRATEGY_MODE_HIGHLIGHT'
la['ToleranceIn'] = 3
la['ToleranceOut'] = 8
la['Evbias'] = -50  # -0.5 EV 强光压制
la['AecMeasType'] = 'AECV2_MEASURETYPE_MEAN'

mc['ae_calib']['CommCtrl']['AecSpeed']['DampOver'] = 0.7
mc['ae_calib']['CommCtrl']['AecSpeed']['DampUnder'] = 0.3
mc['ae_calib']['CommCtrl']['AecAntiFlicker']['enable'] = 0

# 先写临时文件再原子替换，避免半写损坏
tmp = IQ + '.tmp'
with open(tmp, 'w') as f:
    json.dump(data, f, indent='\t')

# 写入后验证
with open(tmp, 'r') as f:
    json.load(f)  # 解析失败会抛异常

os.replace(tmp, IQ)  # 原子替换
PYEOF
    info "  ✓ IQ 已更新（临时文件+原子替换+校验）"
}

restore_iq_original() {
    if [ -f "$IQ_BAK" ]; then
        cp "$IQ_BAK" "$IQ_FILE"
        info "  已恢复原始 IQ 配置"
    else
        warn "  未找到备份文件: $IQ_BAK"
    fi
}

restart_rkaiq() {
    info "重启 rkaiq_3A_server ..."
    killall rkaiq_3A_server 2>/dev/null || true
    sleep 1
    /usr/bin/rkaiq_3A_server 2>&1 | logger -t rkaiq &
    sleep 3

    if pgrep -x rkaiq_3A_server >/dev/null 2>&1; then
        info "  rkaiq ✓ 已重启"
    else
        warn "  rkaiq 启动失败！正在恢复原始配置..."
        restore_iq_original
        /usr/bin/rkaiq_3A_server 2>&1 | logger -t rkaiq &
        sleep 2
    fi
}

# =============================================================================
# Show frame interval from DPHY sink pad
# =============================================================================
show_frame_interval() {
    local interval=$(media-ctl -d "$MEDIA_DEV" --get-v4l2 "'$DPHY':0" 2>/dev/null \
        | grep -oP '@\K\d+/\d+' | head -1)
    if [ -n "$interval" ]; then
        local num="${interval%%/*}"
        local den="${interval##*/}"
        local fps=$(echo "scale=2; $den / $num" | bc 2>/dev/null || echo "?")
        echo "  Frame interval: $interval → ${fps} fps"
    else
        echo "  (无法读取帧率)"
    fi
}

# =============================================================================
# status
# =============================================================================
cmd_status() {
    find_sensor_subdev

    echo "=============================================="
    echo " IMX219 Pipeline Status"
    echo "=============================================="

    echo ""
    echo "--- Sensor (${SENSOR}) ---"
    v4l2-ctl -d "$SENSOR_SUBDEV" --get-subdev-fmt 0 2>/dev/null \
        | grep -E 'Width|Code' | sed 's/^/  /'

    echo ""
    echo "--- CSI D-PHY ---"
    media-ctl -d "$MEDIA_DEV" --get-v4l2 "'$DPHY':0" 2>/dev/null \
        | grep 'fmt:' | sed 's/^/  /'
    show_frame_interval

    echo ""
    echo "--- ISP Subdev ---"
    media-ctl -d "$MEDIA_DEV" --get-v4l2 "'$ISP_SUBDEV':0" 2>/dev/null \
        | grep -E 'fmt:|crop:' | sed 's/^/  /'
    echo "    → source:"
    media-ctl -d "$MEDIA_DEV" --get-v4l2 "'$ISP_SUBDEV':2" 2>/dev/null \
        | grep -E 'fmt:|crop:' | sed 's/^/      /'

    echo ""
    echo "--- $VIDEO_DEV ---"
    v4l2-ctl -d "$VIDEO_DEV" --get-fmt-video 2>/dev/null \
        | grep -E 'Width|Height|Pixel' | sed 's/^/  /'

    echo ""
    echo "--- rkaiq ---"
    if pgrep -x rkaiq_3A_server >/dev/null 2>&1; then
        echo "  rkaiq_3A_server: ✓ 运行中"
    else
        echo "  rkaiq_3A_server: ✗ 未运行 (ISP无法输出帧!)"
    fi
}

# =============================================================================
# set - configure full pipeline for target resolution
# =============================================================================
cmd_set() {
    local mode="$1"
    local skip_fix="${2:-fix}"  # 第二个参数: "nofix" 跳过条纹修复, 默认自动修复
    read -r WIDTH HEIGHT <<< "$(parse_resolution "$mode")"
    read -r ACTUAL_MODE EXPECTED_FPS <<< "$(get_actual_sensor_mode "$mode")"
    find_sensor_subdev

    echo "=============================================="
    echo " IMX219: 请求 ${WIDTH}x${HEIGHT}"
    if [ "${WIDTH}x${HEIGHT}" != "$ACTUAL_MODE" ]; then
        echo "         传感器实际模式: ${ACTUAL_MODE} (驱动就近匹配)"
    fi
    echo "         目标帧率: ${EXPECTED_FPS} fps"
    echo "=============================================="

    # ───────────────────────────────────────────
    # Step 0: 必须先停掉 rkaiq，否则底层改管线时 ISP 会进入错误状态 → 全黑
    # ───────────────────────────────────────────
    info "0/5 停止 rkaiq_3A_server (避免管线配置冲突)"
    killall rkaiq_3A_server 2>/dev/null || true
    sleep 1

    # -----------------------------
    # Step 1-2: Sensor → DPHY → CSI
    # The upstream chain determines actual sensor readout mode & frame rate
    # -----------------------------

    # Set format on sensor subdev (this is what triggers sensor mode switch)
    info "1/5 传感器 → ${WIDTH}x${HEIGHT}"
    v4l2-ctl -d "$SENSOR_SUBDEV" \
        --set-subdev-fmt pad=0,width="$WIDTH",height="$HEIGHT",code=0x300f \
        2>/dev/null

    # DPHY sink: format + frame interval in one call
    info "2/5 CSI D-PHY → ${WIDTH}x${HEIGHT}"
    media-ctl -d "$MEDIA_DEV" --set-v4l2 \
        "'$DPHY':0[fmt:${MBUS_FMT}/${WIDTH}x${HEIGHT}]" 2>/dev/null

    # CSI subdev: both sink(pad0) and source(pad1)
    info "3/5 CSI subdev → ${WIDTH}x${HEIGHT}"
    media-ctl -d "$MEDIA_DEV" --set-v4l2 \
        "'$CSI_SUBDEV':0[fmt:${MBUS_FMT}/${WIDTH}x${HEIGHT}]" 2>/dev/null
    media-ctl -d "$MEDIA_DEV" --set-v4l2 \
        "'$CSI_SUBDEV':1[fmt:${MBUS_FMT}/${WIDTH}x${HEIGHT}]" 2>/dev/null

    # -----------------------------
    # Step 4: ISP subdev - sink crop + source format
    # -----------------------------
    info "4/5 ISP subdev → ${WIDTH}x${HEIGHT}"
    # Sink: format + crop in one command
    media-ctl -d "$MEDIA_DEV" --set-v4l2 \
        "'$ISP_SUBDEV':0[fmt:${MBUS_FMT}/${WIDTH}x${HEIGHT} crop:(0,0)/${WIDTH}x${HEIGHT}]" \
        2>/dev/null
    # Source: YUYV8_2X8 at target resolution
    media-ctl -d "$MEDIA_DEV" --set-v4l2 \
        "'$ISP_SUBDEV':2[fmt:YUYV8_2X8/${WIDTH}x${HEIGHT}]" \
        2>/dev/null

    # -----------------------------
    # Step 5: Video device (rkaiq 已停，设置应该成功)
    # -----------------------------
    info "5/5 ${VIDEO_DEV} → NV12 ${WIDTH}x${HEIGHT}"
    v4l2-ctl -d "$VIDEO_DEV" \
        --set-fmt-video=width="$WIDTH",height="$HEIGHT",pixelformat=NV12 \
        2>/dev/null || true

    echo ""
    info "管线配置完成！ ${WIDTH}x${HEIGHT}"
    echo -n "  传感器帧率: "
    show_frame_interval

    # Step 6: Banding fix (optional)
    if [ "$skip_fix" != "nofix" ]; then
        echo ""
        info "6/6 修复水平条纹 ..."
        patch_iq_for_banding
    fi

    # ───────────────────────────────────────────────
    # Step 7: 重新启动 rkaiq (不论是否 fix 都必须重启，否则 ISP 不处理帧 → 全黑)
    # ───────────────────────────────────────────────
    echo ""
    info "7/6 启动 rkaiq_3A_server ..."
    /usr/bin/rkaiq_3A_server 2>&1 | logger -t rkaiq &
    sleep 3

    if pgrep -x rkaiq_3A_server >/dev/null 2>&1; then
        info "  rkaiq ✓ 已重启"
    else
        warn "  rkaiq 启动失败！"
        if [ "$skip_fix" != "nofix" ]; then
            warn "  正在恢复原始 IQ 配置并重试..."
            restore_iq_original
            /usr/bin/rkaiq_3A_server 2>&1 | logger -t rkaiq &
            sleep 2
        fi
    fi

    if [ "$skip_fix" != "nofix" ]; then
        echo ""
        info "全部完成！ ${WIDTH}x${HEIGHT} — 管线已配置 + 水平条纹已修复"
        echo ""
        echo "  修改内容:"
        echo "    ✓ 模拟增益: 11x → 2x (消除水平条纹 + 防太阳过曝)"
        echo "    ✓ 最大曝光: 100ms → 10ms (强光压制)"
        echo "    ✓ AE 策略: LOWLIGHT → HIGHLIGHT_FIRST"
        echo "    ✓ EV 补偿: -0.50 (强光负补偿)"
        echo "    ✓ 过曝阻尼: 0.3 → 0.7 (快速降曝)"
        echo "    ✓ 防闪烁: 已禁用"
        echo ""
        echo "  ⚠ 请重启编码应用以使用新参数。"
        echo "  如需跳过修复: ./imx219-config-tool.sh set 1080p nofix"
        echo "  如需恢复原始: ./imx219-config-tool.sh banding revert"
    else
        echo ""
        info "全部完成！ ${WIDTH}x${HEIGHT} — 管线已配置 (未修复条纹)"
    fi
}

# =============================================================================
# test - measure actual FPS
# =============================================================================
cmd_test() {
    local count="${1:-60}"
    find_sensor_subdev

    # Show current mode
    local sensor_fmt=$(v4l2-ctl -d "$SENSOR_SUBDEV" --get-subdev-fmt 0 2>/dev/null \
        | grep Width | xargs)
    info "当前传感器: $sensor_fmt"
    info "采集 ${count} 帧..."

    local result
    result=$(v4l2-ctl -d "$VIDEO_DEV" \
        --stream-mmap=3 \
        --stream-count="$count" \
        --stream-to=/dev/null 2>&1)

    # Extract FPS from v4l2-ctl output (most accurate)
    local v4l2_fps=$(echo "$result" | grep -oP '[\d.]+(?= fps)')
    local elapsed=$(echo "$result" | grep -oP '(?<=, )\d+\.\d+(?= s)' || true)

    echo ""
    echo "=============================================="
    echo " 采集 ${count} 帧"
    if [ -n "$v4l2_fps" ]; then
        echo " v4l2-ctl 报告帧率: ${v4l2_fps} fps"
    fi
    if [ -n "$elapsed" ]; then
        echo " 耗时: ${elapsed}s"
    fi
    echo "=============================================="
}

# =============================================================================
# init - startup mode
# =============================================================================
cmd_init() {
    info "IMX219 初始化脚本"

    # Wait for devices
    for i in $(seq 1 20); do
        [ -e "$MEDIA_DEV" ] && [ -e "$VIDEO_DEV" ] && break
        sleep 0.5
    done
    [ -e "$MEDIA_DEV" ] || die "$MEDIA_DEV 不存在，检查 overlay 是否加载"

    # Wait for rkaiq
    info "等待 rkaiq_3A_server..."
    for i in $(seq 1 30); do
        if pgrep -x rkaiq_3A_server >/dev/null 2>&1; then
            sleep 2
            info "rkaiq 已就绪"
            break
        fi
        [ "$i" -eq 30 ] && warn "rkaiq 超时未启动"
        sleep 1
    done

    cmd_set "1080p"
    echo ""
    info "初始化完成 — 摄像头就绪 (1920x1080 @ 30fps)"
}

# =============================================================================
# Main
# =============================================================================
CMD="${1:-status}"
shift 2>/dev/null || true

case "$CMD" in
    status|show|info)
        cmd_status ;;
    set|config|configure)
        cmd_set "${1:-1080p}" "${2:-fix}" ;;
    test|bench|benchmark)
        cmd_test "${1:-60}" ;;
    init|startup)
        cmd_init ;;
    banding|band|fix)
        case "${1:-fix}" in
            fix|apply|on)
                echo "=============================================="
                echo " 修复 IMX219 水平条纹"
                echo "=============================================="
                patch_iq_for_banding
                restart_rkaiq
                echo ""
                info "修复完成！请重启编码应用。"
                ;;
            revert|undo|off)
                echo "=============================================="
                echo " 恢复原始 IQ 配置"
                echo "=============================================="
                restore_iq_original
                restart_rkaiq
                info "已恢复！请重启编码应用。"
                ;;
            status|check)
                echo "=============================================="
                echo " IMX219 条纹修复状态"
                echo "=============================================="
                echo ""
                if [ -f "$IQ_BAK" ]; then
                    echo "  原始备份: ✓ $IQ_BAK"
                else
                    echo "  原始备份: ✗ 无备份"
                fi
                python3 -c "
import json
try:
    with open('$IQ_FILE') as f:
        d = json.load(f)
    sc = d['sensor_calib']
    ac = sc['CISGainSet']['CISAgainRange']
    mc = d['main_scene'][0]['sub_scene'][0]['scene_isp21']
    la = mc['ae_calib']['LinearAeCtrl']
    af = mc['ae_calib']['CommCtrl']['AecAntiFlicker']
    print(f'  模拟增益上限: {ac[\"Max\"]} (修复后应为 2)')
    print(f'  AE 策略: {la[\"StrategyMode\"]}')
    print(f'  EV 补偿: {la[\"Evbias\"]}')
    print(f'  防闪烁: {\"开\" if af[\"enable\"] else \"关\"}')
    print()
    if ac['Max'] <= 5:
        print('  ✓ 修复已应用')
    else:
        print('  ✗ 修复未应用 (模拟增益 > 5)')
except:
    print('  ✗ 无法读取 IQ 配置文件')
" 2>/dev/null || echo "  ✗ Python 读取失败"
                ;;
            *)
                echo "用法: $0 banding {fix|revert|status}" ;;
        esac ;;
    help|-h|--help)
        cat << 'EOF'
IMX219 Pipeline Configuration Tool (Rockchip BSP Kernel)
─────────────────────────────────────────────────────────
用法: imx219-config-tool.sh <command> [args]

命令:
  status              查看当前管线状态
  set    <mode>       设置分辨率 + 自动修复水平条纹
  set    <mode> nofix 设置分辨率但不修复条纹
  test   [count]      测试实际帧率（默认 60 帧）
  init                系统启动初始化（等待 rkaiq → 配置 1080p@30fps + 修复）
  banding fix         单独修复水平条纹
  banding revert      恢复原始 IQ 配置
  banding status      查看条纹修复状态

示例:
  ./imx219-config-tool.sh status
  ./imx219-config-tool.sh set 1080p          # 1080p@30 + 自动修复
  ./imx219-config-tool.sh set full nofix     # 全尺寸不修复条纹
  ./imx219-config-tool.sh test 60
  ./imx219-config-tool.sh init
  ./imx219-config-tool.sh banding fix        # 单独修复
  ./imx219-config-tool.sh banding revert     # 恢复原始

  ⚠ BSP 内核限制：VBLANK 只读，所有模式最大 30fps，不支持 60/90fps

  请求模式    → 传感器实际模式     帧率  说明
  ─────────────────────────────────────────────────
  1080p       → 1920x1080 (裁剪)   30fps  推荐，视频推流最佳
  medium      → 1640x1232 (2x2合并) 30fps  中等分辨率
  720p        → 1640x1232 (就近匹配) 30fps  驱动自动就近匹配
  full        → 3280x2464 (全尺寸)  21fps  全分辨率拍照
  480p        → 640x480   (裁剪)   30fps  低分辨率

  水平条纹修复 (set/init 默认自动)：
    模拟增益上限 11x → 4x  （核心: 消除行噪声）
    AE 策略 LOWLIGHT → HIGHLIGHT_FIRST
    EV +0.25 防欠曝
    防闪烁禁用
EOF
        ;;
esac
