#!/usr/bin/env python3
"""
Simple WebSocket test server for mod_audio_stream testing
Receives audio data and prints statistics
"""
import asyncio
import websockets
import json
import time
import logging
from datetime import datetime

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class AudioStreamStats:
    def __init__(self):
        self.start_time = time.time()
        self.total_bytes = 0
        self.message_count = 0
        self.last_stats_time = time.time()
        self.last_stats_bytes = 0
        self.last_stats_messages = 0
        
    def update(self, data_length):
        self.total_bytes += data_length
        self.message_count += 1
        
        # Print stats every 5 seconds
        current_time = time.time()
        if current_time - self.last_stats_time >= 5.0:
            duration = current_time - self.last_stats_time
            bytes_rate = (self.total_bytes - self.last_stats_bytes) / duration
            msg_rate = (self.message_count - self.last_stats_messages) / duration
            
            logger.info(f"📊 Stats: {self.message_count} messages, "
                       f"{self.total_bytes} bytes total, "
                       f"{bytes_rate:.1f} B/s, {msg_rate:.1f} msg/s")
            
            self.last_stats_time = current_time
            self.last_stats_bytes = self.total_bytes
            self.last_stats_messages = self.message_count
    
    def final_stats(self):
        duration = time.time() - self.start_time
        avg_rate = self.total_bytes / duration if duration > 0 else 0
        logger.info(f"🏁 Final stats: {self.message_count} messages, "
                   f"{self.total_bytes} bytes in {duration:.1f}s, "
                   f"avg rate: {avg_rate:.1f} B/s")

stats = AudioStreamStats()

async def handle_audio_stream(websocket, path):
    """Handle incoming audio stream WebSocket connection"""
    client_addr = websocket.remote_address
    logger.info(f"🔗 New connection from {client_addr[0]}:{client_addr[1]} on path: {path}")
    
    try:
        # Send connection success message
        welcome_msg = {
            "event": "connection_established",
            "timestamp": datetime.now().isoformat(),
            "message": "Ready to receive audio stream"
        }
        await websocket.send(json.dumps(welcome_msg))
        
        async for message in websocket:
            try:
                if isinstance(message, str):
                    # JSON control message
                    data = json.loads(message)
                    logger.info(f"📝 Control message: {data}")
                    
                    # Echo back acknowledgment  
                    response = {
                        "event": "message_received",
                        "timestamp": datetime.now().isoformat(),
                        "original": data
                    }
                    await websocket.send(json.dumps(response))
                    
                    # Test new event formats occasionally
                    if stats.message_count % 50 == 0:
                        # Send test transcription
                        transcription_msg = {
                            "event": "transcription.send",
                            "streamId": "test_stream",
                            "payload": {
                                "text": f"Test transcription #{stats.message_count}",
                                "confidence": 0.95,
                                "isFinal": True,
                                "timestamp": int(time.time() * 1000)
                            }
                        }
                        await websocket.send(json.dumps(transcription_msg))
                        
                    if stats.message_count % 100 == 0:
                        # Send test media play command
                        media_play_msg = {
                            "event": "media.play",
                            "streamId": "test_stream",
                            "payload": {
                                "audioData": "SGVsbG8gV29ybGQ=",  # "Hello World" in base64
                                "sampleRate": 8000,
                                "format": "LINEAR16",
                                "checkpoint": f"test_checkpoint_{stats.message_count}"
                            }
                        }
                        await websocket.send(json.dumps(media_play_msg))
                    
                elif isinstance(message, bytes):
                    # Binary audio data
                    stats.update(len(message))
                    
                    # For bidirectional testing, we could send audio back
                    # For now, just log receipt
                    if stats.message_count % 100 == 0:  # Log every 100th message
                        logger.debug(f"🎵 Received audio frame #{stats.message_count}, "
                                   f"{len(message)} bytes")
                        
            except json.JSONDecodeError as e:
                logger.error(f"❌ Invalid JSON message: {e}")
            except Exception as e:
                logger.error(f"❌ Error processing message: {e}")
                
    except websockets.exceptions.ConnectionClosedOK:
        logger.info(f"✅ Connection from {client_addr[0]}:{client_addr[1]} closed normally")
    except websockets.exceptions.ConnectionClosedError as e:
        logger.warning(f"⚠️ Connection from {client_addr[0]}:{client_addr[1]} closed with error: {e}")
    except Exception as e:
        logger.error(f"❌ Unexpected error handling connection: {e}")
    finally:
        stats.final_stats()

async def main():
    """Start the WebSocket test server"""
    host = "0.0.0.0"  # Listen on all interfaces
    port = 9090
    
    logger.info(f"🚀 Starting WebSocket test server on {host}:{port}")
    logger.info(f"📡 Ready to receive audio streams from FreeSWITCH mod_audio_stream")
    logger.info(f"🔧 Test with: uuid_audio_stream <uuid> <stream_id> start ws://<host>:8080/audio both l16 8000 30 0")
    
    try:
        async with websockets.serve(handle_audio_stream, host, port):
            logger.info("✅ WebSocket server is running...")
            await asyncio.Future()  # Run forever
    except Exception as e:
        logger.error(f"❌ Failed to start server: {e}")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("\n👋 Server shutdown requested")
        stats.final_stats()
    except Exception as e:
        logger.error(f"❌ Server error: {e}")