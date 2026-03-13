#!/usr/bin/env python
#
# Python signaling server example for libdatachannel
# Copyright (c) 2020 Paul-Louis Ageneau
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import sys
import ssl
import json
import asyncio
import logging
import websockets


logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))

clients = {}


async def handle_websocket(websocket):
    # 拒绝非WebSocket路径的连接(比如根路径)
    if websocket.request.path == '/':
        await websocket.close(code=1003, reason='WebSocket endpoint required')
        print('Rejected connection to root path (use /<client_id>)')
        return

    client_id = None
    try:
        splitted = websocket.request.path.split('/')
        splitted.pop(0)
        if not splitted:
            await websocket.close(code=1008, reason='Client ID required')
            print('Rejected connection: no client ID provided')
            return
        client_id = splitted.pop(0)
        print('Client {} connected from {}'.format(client_id, websocket.remote_address))

        clients[client_id] = websocket
        while True:
            data = await websocket.recv()
            print('Client {} << {}'.format(client_id, data))
            message = json.loads(data)
            destination_id = message['id']
            destination_websocket = clients.get(destination_id)
            if destination_websocket:
                message['id'] = client_id
                data = json.dumps(message)
                print('Client {} >> {}'.format(destination_id, data))
                await destination_websocket.send(data)
            else:
                print('Client {} not found'.format(destination_id))

    except websockets.exceptions.ConnectionClosed as e:
        print('Client {} disconnected: {}'.format(client_id, e))

    except websockets.exceptions.InvalidMessage as e:
        # 非WebSocket请求或连接在握手前关闭,忽略这些错误
        print('Invalid connection attempt (not a WebSocket): {}'.format(e))

    except Exception as e:
        print('Error handling client {}: {}'.format(client_id if client_id else 'unknown', e))

    finally:
        if client_id and client_id in clients:
            del clients[client_id]
            print('Client {} disconnected'.format(client_id))


async def main():
    # Usage: ./server.py [[host:]port] [SSL certificate file]
    endpoint_or_port = sys.argv[1] if len(sys.argv) > 1 else "8000"
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None

    # 默认监听 0.0.0.0 以接受外部连接
    endpoint = endpoint_or_port if ':' in endpoint_or_port else "0.0.0.0:" + endpoint_or_port

    if ssl_cert:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert)
    else:
        ssl_context = None

    print('Listening on {}'.format(endpoint))
    host, port = endpoint.rsplit(':', 1)

    server = await websockets.serve(handle_websocket, host, int(port), ssl=ssl_context)
    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())