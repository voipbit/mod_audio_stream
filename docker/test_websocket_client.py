#!/usr/bin/env python3
"""
Simple WebSocket client for testing the server
"""
import asyncio
import websockets
import json

async def test_client():
    uri = "ws://localhost:9090/audio"
    try:
        async with websockets.connect(uri) as websocket:
            print(f"Connected to {uri}")
            
            # Send a test event
            test_event = {
                "type": "test",
                "message": "Hello from test client",
                "timestamp": "2024-01-01T00:00:00Z"
            }
            
            await websocket.send(json.dumps(test_event))
            print("Sent test event")
            
            # Wait for response
            response = await websocket.recv()
            print(f"Received: {response}")
            
            # Send some fake audio data
            fake_audio = b"fake_audio_data" * 10
            await websocket.send(fake_audio)
            print(f"Sent fake audio data ({len(fake_audio)} bytes)")
            
            print("Test completed successfully")
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    asyncio.run(test_client())