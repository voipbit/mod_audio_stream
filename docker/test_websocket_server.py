#!/usr/bin/env python3
"""
Simple WebSocket server for testing mod_audio_stream
Receives audio data and events from FreeSWITCH
"""
import asyncio
import websockets
import json
import logging
from datetime import datetime

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class AudioStreamTestServer:
    def __init__(self):
        self.clients = {}
        self.audio_frames_received = 0
        self.events_received = 0
        
    async def handle_client(self, websocket):
        client_id = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
        path = websocket.path if hasattr(websocket, 'path') else '/unknown'
        logger.info(f"New client connected: {client_id} on path: {path}")
        
        self.clients[client_id] = {
            'websocket': websocket,
            'connected_at': datetime.now(),
            'audio_frames': 0,
            'events': 0
        }
        
        try:
            async for message in websocket:
                await self.handle_message(client_id, message)
                
        except websockets.exceptions.ConnectionClosed:
            logger.info(f"Client {client_id} disconnected")
        except Exception as e:
            logger.error(f"Error handling client {client_id}: {e}")
        finally:
            if client_id in self.clients:
                client_info = self.clients[client_id]
                duration = datetime.now() - client_info['connected_at']
                logger.info(f"Client {client_id} session ended. Duration: {duration}, "
                          f"Audio frames: {client_info['audio_frames']}, Events: {client_info['events']}")
                del self.clients[client_id]
    
    async def handle_message(self, client_id, message):
        client = self.clients[client_id]
        
        # Check if message is binary (audio data) or text (JSON events)
        if isinstance(message, bytes):
            # Binary audio data
            client['audio_frames'] += 1
            self.audio_frames_received += 1
            
            if client['audio_frames'] % 100 == 0:  # Log every 100th frame
                logger.info(f"Client {client_id}: Received {client['audio_frames']} audio frames "
                          f"(latest: {len(message)} bytes)")
                
        else:
            # Text message - should be JSON events
            try:
                event_data = json.loads(message)
                client['events'] += 1
                self.events_received += 1
                
                logger.info(f"Client {client_id}: Received event - {event_data}")
                
                # Send acknowledgment back
                response = {
                    "type": "ack",
                    "timestamp": datetime.now().isoformat(),
                    "original_event": event_data
                }
                await client['websocket'].send(json.dumps(response))
                
            except json.JSONDecodeError:
                logger.warning(f"Client {client_id}: Received invalid JSON: {message}")
    
    async def status_reporter(self):
        """Periodically report server status"""
        while True:
            await asyncio.sleep(10)
            active_clients = len(self.clients)
            if active_clients > 0 or self.audio_frames_received > 0:
                logger.info(f"Server Status: {active_clients} active clients, "
                          f"{self.audio_frames_received} total audio frames, "
                          f"{self.events_received} total events")

async def main():
    server = AudioStreamTestServer()
    
    # Start status reporter
    asyncio.create_task(server.status_reporter())
    
    # Start WebSocket server on all interfaces
    logger.info("Starting WebSocket server on ws://0.0.0.0:9090/audio")
    
    async with websockets.serve(server.handle_client, "0.0.0.0", 9090):
        logger.info("WebSocket server is ready for connections...")
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())