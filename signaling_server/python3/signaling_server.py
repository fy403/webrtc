#!/usr/bin/env python3
"""
libdatachannel example web server - Python Flask version
Copyright (C) 2020 Lara Mackey
Copyright (C) 2020 Paul-Louis Ageneau

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <http://www.gnu.org/licenses/>.
"""

import asyncio
import websockets
import json
import logging
import os
from flask import Flask, request, jsonify
from flask_cors import CORS
import threading
from urllib.parse import urlparse

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# 存储客户端连接
clients = {}

# 创建Flask应用
app = Flask(__name__)
CORS(app)  # 启用跨域支持

@app.route('/', methods=['GET', 'POST', 'OPTIONS'])
def handle_request():
    """处理HTTP请求"""
    logger.info(f"{request.method} {request.path}")
    return jsonify({'error': 'Not Found'}), 404

@app.route('/health', methods=['GET'])
def health_check():
    """健康检查端点"""
    return jsonify({'status': 'ok', 'clients': len(clients)})

@app.route('/clients', methods=['GET'])
def list_clients():
    """列出所有连接的客户端"""
    return jsonify({'clients': list(clients.keys())})

async def handle_client(websocket, path):
    """处理WebSocket客户端连接"""
    logger.info(f"WS  {path}")
    
    # 解析路径获取客户端ID
    parsed_path = urlparse(path)
    path_parts = parsed_path.path.strip('/').split('/')
    if len(path_parts) < 1 or not path_parts[0]:
        logger.error("Invalid path format")
        await websocket.close()
        return
    
    client_id = path_parts[0]
    clients[client_id] = websocket
    logger.info(f"Client {client_id} connected")
    
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                logger.info(f"Client {client_id} << {message}")
                
                dest_id = data.get('id')
                if dest_id and dest_id in clients:
                    # 将发送者ID设置为目标
                    data['id'] = client_id
                    response = json.dumps(data)
                    logger.info(f"Client {dest_id} >> {response}")
                    await clients[dest_id].send(response)
                else:
                    logger.error(f"Client {dest_id} not found")
                    
            except json.JSONDecodeError:
                logger.error(f"Invalid JSON message from client {client_id}: {message}")
            except Exception as e:
                logger.error(f"Error processing message from client {client_id}: {e}")
                
    except websockets.exceptions.ConnectionClosed:
        logger.info(f"Client {client_id} connection closed")
    except Exception as e:
        logger.error(f"Error handling client {client_id}: {e}")
    finally:
        # 清理客户端连接
        if client_id in clients:
            del clients[client_id]
        logger.info(f"Client {client_id} disconnected")

def run_flask_app(host, port):
    """运行Flask应用"""
    logger.info(f"Flask HTTP Server listening on {host}:{port}")
    app.run(host=host, port=port, debug=False, use_reloader=False)

async def start_websocket_server(host, port):
    """启动WebSocket服务器"""
    server = await websockets.serve(handle_client, host, port)
    logger.info(f"WebSocket Server listening on {host}:{port}")
    await server.wait_closed()

def main():
    """主函数"""
    # 从环境变量获取端口配置
    endpoint = os.environ.get('PORT', '8000')
    
    # 解析host和port
    if ':' in endpoint:
        parts = endpoint.split(':')
        port = int(parts[-1])
        hostname = ':'.join(parts[:-1]) or '127.0.0.1'
    else:
        hostname = '0.0.0.0'
        port = int(endpoint)
    
    logger.info(f"Starting servers on {hostname}:{port}")
    
    # 在单独的线程中启动Flask HTTP服务器
    flask_thread = threading.Thread(target=run_flask_app, args=(hostname, port), daemon=True)
    flask_thread.start()
    
    # 在主线程中启动WebSocket服务器
    try:
        asyncio.run(start_websocket_server(hostname, port))
    except KeyboardInterrupt:
        logger.info("Servers stopped by user")
    except Exception as e:
        logger.error(f"Server error: {e}")

if __name__ == "__main__":
    main()