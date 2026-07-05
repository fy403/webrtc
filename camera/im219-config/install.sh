#!/bin/bash
# =============================================================================
# IMX219 Config Service - 安装脚本
# 将 imx219-config-tool.sh 和 wrapper 安装为 systemd service
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_NAME="imx219-config"
SERVICE_FILE="$SCRIPT_DIR/${SERVICE_NAME}.service"
WRAPPER_FILE="$SCRIPT_DIR/imx219-config-wrapper.sh"
CONFIG_TOOL="$SCRIPT_DIR/imx219-config-tool.sh"

# 颜色
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
die()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ─────────────────────────────────────────────────────────────────────────────
# 检查环境
# ─────────────────────────────────────────────────────────────────────────────
check_root() {
    if [ "$EUID" -ne 0 ]; then
        die "请使用 root 权限运行: sudo $0"
    fi
}

check_systemd() {
    if ! command -v systemctl &>/dev/null; then
        die "系统不支持 systemd"
    fi
}

check_docker() {
    if ! command -v docker &>/dev/null; then
        warn "Docker 未安装，service 中容器管理将跳过"
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# 安装
# ─────────────────────────────────────────────────────────────────────────────
install() {
    info "安装 IMX219 Config Service..."

    # 1. 复制脚本到 /usr/local/bin
    info "1/4 复制脚本到 /usr/local/bin ..."

    cp "$CONFIG_TOOL" /usr/local/bin/imx219-config-tool.sh
    chmod +x /usr/local/bin/imx219-config-tool.sh
    ok "  ✓ imx219-config-tool.sh"

    cp "$WRAPPER_FILE" /usr/local/bin/imx219-config-wrapper.sh
    chmod +x /usr/local/bin/imx219-config-wrapper.sh
    ok "  ✓ imx219-config-wrapper.sh"

    # 2. 安装 systemd service
    info "2/4 安装 systemd service ..."
    cp "$SERVICE_FILE" /etc/systemd/system/${SERVICE_NAME}.service
    ok "  ✓ ${SERVICE_NAME}.service"

    # 3. 重载 systemd
    info "3/4 重载 systemd ..."
    systemctl daemon-reload
    ok "  ✓ systemd 已重载"

    # 4. 启用 service（开机自启）
    info "4/4 启用开机自启 ..."
    systemctl enable "${SERVICE_NAME}.service"
    ok "  ✓ service 已启用"

    echo ""
    echo -e "${GREEN}=============================================="
    echo " 安装完成！"
    echo "==============================================${NC}"
    echo ""
    echo " 使用说明:"
    echo "   sudo systemctl start   ${SERVICE_NAME}    # 立即运行（停止容器→配置→启动容器）"
    echo "   sudo systemctl stop    ${SERVICE_NAME}    # 无操作（oneshot 类型）"
    echo "   sudo systemctl status  ${SERVICE_NAME}    # 查看状态"
    echo "   sudo systemctl disable ${SERVICE_NAME}    # 禁用开机自启"
    echo ""
    echo "  开机自动运行: ✓ 已启用"
    echo "  日志查看: sudo journalctl -u ${SERVICE_NAME}"
    echo ""
}

# ─────────────────────────────────────────────────────────────────────────────
# 卸载
# ─────────────────────────────────────────────────────────────────────────────
uninstall() {
    info "卸载 IMX219 Config Service..."

    systemctl stop "${SERVICE_NAME}.service" 2>/dev/null || true
    systemctl disable "${SERVICE_NAME}.service" 2>/dev/null || true

    rm -f /etc/systemd/system/${SERVICE_NAME}.service
    rm -f /usr/local/bin/imx219-config-tool.sh
    rm -f /usr/local/bin/imx219-config-wrapper.sh

    systemctl daemon-reload

    ok "卸载完成"
}

# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────
case "${1:-install}" in
    install|i)
        check_root
        check_systemd
        check_docker
        install
        ;;
    uninstall|remove|u)
        check_root
        uninstall
        ;;
    *)
        echo "用法: $0 [install|uninstall]"
        echo "  install   (默认) 安装 service"
        echo "  uninstall         卸载 service"
        exit 1
        ;;
esac
