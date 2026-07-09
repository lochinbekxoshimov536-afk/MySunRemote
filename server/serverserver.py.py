#!/usr/bin/env python3
"""
MySunRemote WebRTC signalizatsiya serveri
"""
import asyncio
import json
import logging
import ssl
import os
from dataclasses import dataclass, field
from typing import Dict

import websockets
from websockets.server import WebSocketServerProtocol

logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
log = logging.getLogger('server')

HOST = '0.0.0.0'
PORT = 8765
WS_PATH = '/ws'

@dataclass
class Room:
    password: str
    clients: Dict[str, WebSocketServerProtocol] = field(default_factory=dict)

class SignalingServer:
    def __init__(self):
        self.rooms: Dict[str, Room] = {}
        self.ws_to_room: Dict[WebSocketServerProtocol, tuple] = {}

    async def handler(self, ws: WebSocketServerProtocol):
        log.info(f"New connection: {ws.remote_address}")
        try:
            async for raw in ws:
                try:
                    data = json.loads(raw)
                except json.JSONDecodeError:
                    await self.send_error(ws, "Invalid JSON")
                    continue
                if not isinstance(data, dict) or 'type' not in data:
                    await self.send_error(ws, "Missing 'type' field")
                    continue
                await self.route(ws, data)
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            await self.cleanup(ws)

    async def route(self, ws: WebSocketServerProtocol, msg: dict):
        t = msg.get('type')
        if t == 'join':
            await self.handle_join(ws, msg)
            return
        if ws not in self.ws_to_room:
            await self.send_error(ws, "You must join first")
            return
        room_id, client_id = self.ws_to_room[ws]
        if t in ('offer', 'answer', 'ice_candidate'):
            await self.relay(ws, room_id, client_id, msg)
        else:
            await self.send_error(ws, f"Unknown type: {t}")

    async def handle_join(self, ws: WebSocketServerProtocol, msg: dict):
        room = msg.get('room')
        client_id = msg.get('client_id')
        password = msg.get('password', '')
        if not room or not client_id:
            await self.send_error(ws, "Missing room or client_id")
            return

        if room not in self.rooms:
            self.rooms[room] = Room(password=password)
            log.info(f"Room {room} created by {client_id}")

        self.rooms[room].clients[client_id] = ws
        self.ws_to_room[ws] = (room, client_id)

        await self.send_to(ws, {'type': 'joined', 'room': room, 'client_id': client_id})

        for cid, cws in self.rooms[room].clients.items():
            if cid != client_id:
                await self.send_to(cws, {'type': 'peer_joined', 'from': client_id})

    async def relay(self, ws: WebSocketServerProtocol, room_id: str,
                    sender_id: str, msg: dict):
        room = self.rooms.get(room_id)
        if not room: return
        target = msg.get('target')
        out = dict(msg)
        out['from'] = sender_id

        if target:
            target_ws = room.clients.get(target)
            if target_ws:
                await self.send_to(target_ws, out)
            else:
                await self.send_error(ws, f"Target {target} not found")
        else:
            for cid, cws in room.clients.items():
                if cid != sender_id:
                    await self.send_to(cws, out)

    async def cleanup(self, ws: WebSocketServerProtocol):
        info = self.ws_to_room.pop(ws, None)
        if info is None: return
        room_id, client_id = info
        room = self.rooms.get(room_id)
        if room:
            room.clients.pop(client_id, None)
            for cid, cws in room.clients.items():
                await self.send_to(cws, {'type': 'peer_left', 'from': client_id})
            if not room.clients:
                del self.rooms[room_id]
                log.info(f"Room {room_id} deleted")

    async def send_to(self, ws: WebSocketServerProtocol, data: dict):
        try:
            await ws.send(json.dumps(data))
        except:
            pass

    async def send_error(self, ws: WebSocketServerProtocol, msg: str):
        await self.send_to(ws, {'type': 'error', 'message': msg})

def build_ssl():
    cert = 'cert.pem'
    key = 'key.pem'
    if os.path.exists(cert) and os.path.exists(key):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cert, key)
        return ctx
    return None

async def main():
    server = SignalingServer()
    ssl_ctx = build_ssl()
    scheme = 'wss' if ssl_ctx else 'ws'
    async with websockets.serve(server.handler, HOST, PORT, ssl=ssl_ctx,
                                ping_interval=20, ping_timeout=20):
        log.info(f"Server started: {scheme}://{HOST}:{PORT}{WS_PATH}")
        await asyncio.Future()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("Stopped")