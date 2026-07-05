#!/bin/bash
# =============================================================================
# IMX219 Config Wrapper - 停止容器 → 配置 ISP → 启动容器
# 供 systemd service 调用
# =============================================================================

set -e

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
CONFIG_TOOL="$SCRIPT_DIR/imx219-config-tool.sh"

# 需要停止/启动的 Docker 容器列表（按依赖顺序）
# 在运行前停止，运行后启动
CONTAINERS=(
    "webrtc_av_track"
    "webrtc_data_track"
    "webrtc_cmd_track"
)

# 颜色
info()  { echo "[INFO]  $*"; }
warn()  { echo "[WARN]  $*"; }
ok()    { echo "[OK]    $*"; }

# ─────────────────────────────────────────────────────────────────────────────
# 停止所有相关容器
# ─────────────────────────────────────────────────────────────────────────────
stop_containers() {
    info "停止 Docker 容器..."
    for name in "${CONTAINERS[@]}"; do
        if docker ps --format '{{.Names}}' 2>/dev/null | grep -q "^${name}$"; then
            info "  停止 $name ..."
            docker stop "$name" >/dev/null 2>&1 || true
            ok "  $name 已停止"
        else
            info "  $name 未运行，跳过"
        fi
    done
}

# ─────────────────────────────────────────────────────────────────────────────
# 启动所有相关容器
# ─────────────────────────────────────────────────────────────────────────────
start_containers() {
    info "启动 Docker 容器..."
    for name in "${CONTAINERS[@]}"; do
        if docker ps -a --format '{{.Names}}' 2>/dev/null | grep -q "^${name}$"; then
            info "  启动 $name ..."
            docker start "$name" >/dev/null 2>&1 || warn "  $name 启动失败"
            ok "  $name 已启动"
        else
            warn "  $name 不存在，跳过"
        fi
    done
}

# ─────────────────────────────────────────────────────────────────────────────
# 主流程
# ─────────────────────────────────────────────────────────────────────────────
main() {
    echo "=============================================="
    echo " IMX219 Config Service ($(date '+%Y-%m-%d %H:%M:%S'))"
    echo "=============================================="

    # 检查配置文件是否存在
    if [ ! -x "$CONFIG_TOOL" ]; then
        echo "[ERROR] 配置工具不存在: $CONFIG_TOOL"
        exit 1
    fi

    # 1. 停止容器（释放 /dev/video0）
    stop_containers

    # 等待设备释放
    sleep 2

    # 2. 运行 ISP 配置
    info "配置 ISP 管线..."
    "$CONFIG_TOOL" init
    ok "ISP 配置完成"

    # 等待 ISP 稳定
    sleep 2

    # 3. 启动容器
    start_containers

    echo ""
    ok "全部完成！"
    echo "=============================================="
}

main "$@"
