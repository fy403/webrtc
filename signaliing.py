# signaling_server.py
import asyncio
import json
import logging
import websockets
from typing import Dict, Set, List

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class WebRTCSignalingServer:
    def __init__(self):
        self.publishers: Dict[str, websockets.WebSocketServerProtocol] = {}
        self.viewers: Dict[str, websockets.WebSocketServerProtocol] = {}
        self.publisher_offers: Dict[str, Dict] = {}
        self.viewer_offers: Dict[str, Dict] = {}
        
    async def register_publisher(self, websocket: websockets.WebSocketServerProtocol, client_id: str):
        """注册发布者"""
        self.publishers[client_id] = websocket
        logger.info(f"Publisher registered: {client_id}")
        
        # 通知所有观看者有新的发布者
        for viewer_id, viewer_ws in self.viewers.items():
            try:
                await viewer_ws.send(json.dumps({
                    'type': 'new_publisher',
                    'publisher_id': client_id
                }))
                logger.info(f"Notified viewer {viewer_id} about new publisher {client_id}")
            except Exception as e:
                logger.error(f"Error notifying viewer {viewer_id}: {e}")
                
    async def register_viewer(self, websocket: websockets.WebSocketServerProtocol, client_id: str):
        """注册观看者"""
        self.viewers[client_id] = websocket
        logger.info(f"Viewer registered: {client_id}")
        
        # 通知观看者已存在的发布者
        for publisher_id in self.publishers.keys():
            try:
                await websocket.send(json.dumps({
                    'type': 'new_publisher',
                    'publisher_id': publisher_id
                }))
                logger.info(f"Notified viewer {client_id} about existing publisher {publisher_id}")
            except Exception as e:
                logger.error(f"Error notifying viewer {client_id} about publisher {publisher_id}: {e}")
    
    async def handle_viewer_offer(self, viewer_id: str, publisher_id: str, offer: Dict):
        """处理观看者的Offer"""
        logger.info(f"Offer received from viewer {viewer_id} for publisher {publisher_id}")
        
        # 存储 viewer offer
        offer_key = f"{viewer_id}_{publisher_id}"
        self.viewer_offers[offer_key] = offer
        
        # 转发给对应的发布者
        if publisher_id in self.publishers:
            try:
                await self.publishers[publisher_id].send(json.dumps({
                    'type': 'offer',
                    'offer': offer,
                    'viewer_id': viewer_id
                }))
                logger.info(f"Sent offer from viewer {viewer_id} to publisher {publisher_id}")
            except Exception as e:
                logger.error(f"Error sending offer to publisher {publisher_id}: {e}")
        else:
            logger.error(f"Publisher {publisher_id} not found")
    
    async def handle_publisher_answer(self, publisher_id: str, viewer_id: str, answer: Dict):
        """处理发布者的Answer"""
        logger.info(f"Answer received from publisher {publisher_id} for viewer {viewer_id}")
        
        if viewer_id in self.viewers:
            try:
                await self.viewers[viewer_id].send(json.dumps({
                    'type': 'answer',
                    'answer': answer,
                    'publisher_id': publisher_id
                }))
                logger.info(f"Sent answer from publisher {publisher_id} to viewer {viewer_id}")
                
            except Exception as e:
                logger.error(f"Error sending answer to viewer {viewer_id}: {e}")
        else:
            logger.error(f"Viewer {viewer_id} not found")
    
    async def handle_ice_candidate(self, from_id: str, target_id: str, candidate: Dict, client_type: str):
        """处理ICE候选"""
        logger.info(f"ICE candidate from {from_id} to {target_id}, type: {client_type}")
        
        # 观看者发送给特定发布者的 ICE 候选
        if client_type == "viewer" and target_id in self.publishers:
            try:
                await self.publishers[target_id].send(json.dumps({
                    'type': 'ice_candidate',
                    'candidate': candidate,
                    'from_id': from_id
                }))
                logger.info(f"Sent ICE candidate from viewer {from_id} to publisher {target_id}")
            except Exception as e:
                logger.error(f"Error sending ICE candidate to publisher {target_id}: {e}")
            return
        
        # 发布者发送给特定观看者的 ICE 候选
        elif client_type == "publisher" and target_id in self.viewers:
            try:
                await self.viewers[target_id].send(json.dumps({
                    'type': 'ice_candidate',
                    'candidate': candidate,
                    'from_id': from_id
                }))
                logger.info(f"Sent ICE candidate from publisher {from_id} to viewer {target_id}")
            except Exception as e:
                logger.error(f"Error sending ICE candidate to viewer {target_id}: {e}")
            return
        
        logger.warning(f"No target found for ICE candidate from {from_id} to {target_id}")

    async def handle_client_disconnect(self, client_id: str):
        """处理客户端断开连接"""
        logger.info(f"Client disconnected: {client_id}")
        
        if client_id in self.publishers:
            # 清理发布者相关数据
            del self.publishers[client_id]
            if client_id in self.publisher_offers:
                del self.publisher_offers[client_id]
            logger.info(f"Publisher disconnected: {client_id}")
            
            # 通知所有观看者发布者已断开
            for viewer_id, viewer_ws in self.viewers.items():
                try:
                    await viewer_ws.send(json.dumps({
                        'type': 'publisher_disconnected',
                        'publisher_id': client_id
                    }))
                    logger.info(f"Notified viewer {viewer_id} about publisher {client_id} disconnect")
                except Exception as e:
                    logger.error(f"Error notifying viewer {viewer_id} about disconnect: {e}")
                    
        elif client_id in self.viewers:
            # 清理观看者相关数据
            del self.viewers[client_id]
            
            # 清理相关的 viewer offers
            keys_to_remove = [key for key in self.viewer_offers.keys() if key.startswith(f"{client_id}_")]
            for key in keys_to_remove:
                del self.viewer_offers[key]
                logger.info(f"Removed viewer offer: {key}")
                
            logger.info(f"Viewer disconnected: {client_id}")

    async def handle_message(self, websocket: websockets.WebSocketServerProtocol, message: str, client_id: str):
        """处理客户端消息"""
        try:
            data = json.loads(message)
            msg_type = data.get('type')
            
            logger.info(f"Received message from {client_id}: {msg_type}")
            
            if msg_type == 'register_publisher':
                await self.register_publisher(websocket, client_id)
                
            elif msg_type == 'register_viewer':
                await self.register_viewer(websocket, client_id)
                
            elif msg_type == 'offer':
                # Viewer 发送 offer 请求流
                publisher_id = data.get('publisher_id')
                if not publisher_id:
                    logger.error(f"Viewer {client_id} sent offer without publisher_id")
                    return
                    
                await self.handle_viewer_offer(
                    client_id, 
                    publisher_id, 
                    data.get('offer')
                )
                
            elif msg_type == 'answer':
                # Publisher 发送 answer 响应
                viewer_id = data.get('viewer_id')
                if not viewer_id:
                    logger.error(f"Publisher {client_id} sent answer without viewer_id")
                    return
                    
                await self.handle_publisher_answer(
                    client_id,
                    viewer_id,
                    data.get('answer')
                )
                
            elif msg_type == 'ice_candidate':
                candidate = data.get('candidate')
                target_id = data.get('target_id')
                
                if not candidate or not target_id:
                    logger.error(f"ICE candidate missing candidate or target_id from {client_id}")
                    return
                
                # 确定客户端类型
                client_type = "publisher" if client_id in self.publishers else "viewer"
                
                await self.handle_ice_candidate(
                    client_id, 
                    target_id, 
                    candidate,
                    client_type
                )
                
            else:
                logger.warning(f"Unknown message type from {client_id}: {msg_type}")
                
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON message from {client_id}: {e}")
        except Exception as e:
            logger.error(f"Error handling message from {client_id}: {e}")

    async def handler(self, websocket: websockets.WebSocketServerProtocol, path: str):
        """WebSocket连接处理器"""
        client_id = None
        try:
            async for message in websocket:
                # 解析消息获取client_id
                try:
                    data = json.loads(message)
                    client_id = data.get('client_id')
                    if not client_id:
                        logger.error("Message missing client_id")
                        continue
                except json.JSONDecodeError:
                    logger.error("Invalid JSON message - cannot extract client_id")
                    continue
                
                await self.handle_message(websocket, message, client_id)
                
        except websockets.exceptions.ConnectionClosed:
            logger.info(f"Client connection closed: {client_id}")
        except Exception as e:
            logger.error(f"Error in handler for client {client_id}: {e}")
        finally:
            if client_id:
                await self.handle_client_disconnect(client_id)

async def main():
    server = WebRTCSignalingServer()
    async with websockets.serve(server.handler, "0.0.0.0", 1985):
        logger.info("Signaling server running on ws://0.0.0.0:1985")
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    asyncio.run(main())