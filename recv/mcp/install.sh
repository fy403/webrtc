#!/bin/bash
# =============================================================================
# install.sh - Install dependencies and build MCP server
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================"
echo "  Remote Control MCP Server - Installer"
echo "============================================"

# Check Node.js
if ! command -v node &> /dev/null; then
    echo "[ERROR] Node.js is not installed. Please install Node.js >= 18.0.0"
    echo "  Ubuntu: curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash - && sudo apt-get install -y nodejs"
    echo "  Or:     sudo apt-get install -y nodejs npm"
    exit 1
fi

NODE_VERSION=$(node -v | cut -d'v' -f2 | cut -d'.' -f1)
echo "[INFO] Node.js version: $(node -v)"

if [ "$NODE_VERSION" -lt 18 ]; then
    echo "[ERROR] Node.js >= 18.0.0 required. Current: $(node -v)"
    exit 1
fi

# Check npm
if ! command -v npm &> /dev/null; then
    echo "[ERROR] npm is not installed. Please install npm first."
    exit 1
fi

echo "[INFO] npm version: $(npm -v)"

# werift is pure TypeScript - no native WebRTC dependencies needed.
echo "[INFO] Using werift (pure TypeScript WebRTC) - no native build deps required."

# Install npm dependencies
echo "[INFO] Installing npm dependencies..."
npm install

# Build TypeScript
echo "[INFO] Building TypeScript..."
npm run build

echo ""
echo "============================================"
echo "  Installation Complete!"
echo "============================================"
echo ""
echo "Usage:"
echo "  npm start          # Run with default config.json"
echo "  node dist/index.js # Run directly"
echo ""
echo "To configure:"
echo "  1. Edit config.json to set remoteId and signaling server"
echo "  2. Or use environment variables:"
echo "     REMOTE_ID=device_123 SIGNALING_URL=10.0.0.1 npm start"
echo ""
echo "To register with Claude Desktop, add to claude_desktop_config.json:"
echo '  {'
echo '    "mcpServers": {'
echo '      "remote-control": {'
echo '        "command": "node",'
echo '        "args": ["'$SCRIPT_DIR'/dist/index.js"],'
echo '        "env": {'
echo '          "REMOTE_ID": "your_device_id",'
echo '          "SIGNALING_URL": "119.45.178.251"'
echo '        }'
echo '      }'
echo '    }'
echo '  }'
