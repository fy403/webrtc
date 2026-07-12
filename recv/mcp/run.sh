#!/bin/bash
# =============================================================================
# run.sh - Start the MCP server
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check if built
if [ ! -f "dist/index.js" ]; then
    echo "[ERROR] Server not built. Run ./install.sh first."
    exit 1
fi

# Set defaults
export REMOTE_ID="${REMOTE_ID:-}"
export SIGNALING_URL="${SIGNALING_URL:-119.45.178.251}"
export SIGNALING_PORT="${SIGNALING_PORT:-8000}"

echo "============================================"
echo "  Remote Control MCP Server"
echo "============================================"
echo "  Signaling: ${SIGNALING_URL}:${SIGNALING_PORT}"
echo "  Remote ID: ${REMOTE_ID:-(not set - must call device_connect)}"
echo ""
echo "  This server communicates via stdio (stdin/stdout)"
echo "  for MCP protocol integration."
echo ""
echo "  To use with Claude Desktop, add to config:"
echo '  {'
echo '    "mcpServers": {'
echo '      "remote-control": {'
echo '        "command": "node",'
echo '        "args": ["'"$SCRIPT_DIR"'/dist/index.js"],'
echo '        "env": {'
echo '          "REMOTE_ID": "'"${REMOTE_ID:-your_device_id}"'",'
echo '          "SIGNALING_URL": "'"$SIGNALING_URL"'"'
echo '        }'
echo '      }'
echo '    }'
echo '  }'
echo "============================================"

exec node dist/index.js
