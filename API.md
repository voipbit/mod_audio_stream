# mod_audio_stream API Documentation

## Overview

The `mod_audio_stream` module provides a comprehensive API for real-time audio streaming over WebSocket connections. This document details all available commands, events, configuration options, and usage patterns.

## Table of Contents

- [API Commands](#api-commands)
- [Event Types](#event-types)  
- [Configuration](#configuration)
- [WebSocket Protocol](#websocket-protocol)
- [Examples](#examples)
- [Error Handling](#error-handling)

## API Commands

### uuid_audio_stream

The primary API command for controlling audio streaming sessions.

#### Syntax

```
uuid_audio_stream <uuid> <stream_id> <action> [parameters...]
```

#### Parameters

- `uuid`: FreeSWITCH session UUID
- `stream_id`: Unique identifier for the stream (user-defined)
- `action`: Action to perform (see below)

#### Actions

##### start

Start a new audio streaming session.

```
uuid_audio_stream <uuid> <stream_id> start <wss_url> <track_type> <sampling_rate> <timeout> <bidirectional> [metadata]
```

**Parameters:**
- `wss_url`: WebSocket server URL (ws:// or wss://)
- `track_type`: Audio track selection
  - `inbound`: Stream audio from caller
  - `outbound`: Stream audio to caller  
  - `both`: Stream both directions
- `sampling_rate`: Audio sampling rate in Hz
  - `8000`, `16000`, `24000`, `32000`, `44100`, `48000`
- `timeout`: Connection timeout in seconds (0 = no timeout)
- `bidirectional`: Enable bidirectional mode (0 or 1)
- `metadata`: Optional JSON metadata to send with stream start

**Example:**
```bash
uuid_audio_stream ${uuid} my_stream_1 start wss://api.example.com/stream inbound 16000 30 1 '{"session_info":"test"}'
```

##### stop

Stop an active streaming session.

```
uuid_audio_stream <uuid> <stream_id> stop [reason]
```

**Parameters:**
- `reason`: Optional termination reason

**Example:**
```bash
uuid_audio_stream ${uuid} my_stream_1 stop "User requested"
```

##### pause

Pause audio streaming (connection remains active).

```
uuid_audio_stream <uuid> <stream_id> pause
```

##### resume

Resume paused audio streaming.

```
uuid_audio_stream <uuid> <stream_id> resume
```

##### graceful-shutdown

Initiate graceful shutdown with buffered data flush.

```
uuid_audio_stream <uuid> <stream_id> graceful-shutdown [reason]
```

##### send_text

Send text message to remote server (bidirectional mode only).

```
uuid_audio_stream <uuid> <stream_id> send_text <json_message>
```

**Example:**
```bash
uuid_audio_stream ${uuid} my_stream_1 send_text '{"command":"mute","value":true}'
```

## Event Types

The module emits various FreeSWITCH events during operation:

### Connection Events

#### mod_audio_stream::connection_established
Fired when WebSocket connection is established successfully.

**Body:** JSON with connection details
```json
{
  "streamId": "my_stream_1",
  "serverUrl": "wss://api.example.com/stream",
  "connectionTime": "2024-01-01T10:00:00Z"
}
```

#### mod_audio_stream::connection_failed
Fired when WebSocket connection fails.

**Body:** JSON with error details
```json
{
  "streamId": "my_stream_1",
  "error": "Connection refused",
  "serverUrl": "wss://api.example.com/stream"
}
```

#### mod_audio_stream::connection_timeout
Fired when connection times out.

#### mod_audio_stream::connection_closed
Fired when WebSocket connection is closed.

### Stream Events

#### mod_audio_stream::stream_started
Fired when streaming begins.

#### mod_audio_stream::stream_stopped
Fired when streaming ends.

#### mod_audio_stream::stream_error
Fired on stream errors.

### Media Events

#### mod_audio_stream::media_play_start
Fired when remote server requests audio playback.

#### mod_audio_stream::media_play_complete
Fired when audio playback completes.

#### mod_audio_stream::transcription_received
Fired when transcription data is received from the server.

**Body:** JSON with transcription details
```json
{
  "streamId": "my_stream_1",
  "payload": {
    "text": "Hello, how can I help you today?",
    "confidence": 0.98,
    "isFinal": true,
    "timestamp": 1694712000000
  }
}
```

### Monitoring Events

#### mod_audio_stream::heartbeat
Periodic heartbeat for stream monitoring.

#### mod_audio_stream::buffer_overrun
Fired when audio buffer overruns occur.

## Configuration

### Environment Variables

The module supports extensive configuration via environment variables:

#### Connection Settings

- `MOD_AUDIO_STREAM_SUBPROTOCOL_NAME`: WebSocket subprotocol name
  - Default: `audio.freeswitch.org`
  - Example: `export MOD_AUDIO_STREAM_SUBPROTOCOL_NAME="my.audio.protocol"`

- `MOD_AUDIO_STREAM_SERVICE_THREADS`: Number of WebSocket service threads
  - Default: `2`
  - Range: `1-5`
  - Example: `export MOD_AUDIO_STREAM_SERVICE_THREADS=3`

#### Buffer Settings

- `MOD_AUDIO_STREAM_BUFFER_SECS`: Audio buffer capacity in seconds
  - Default: `40`
  - Range: `1-40`
  - Example: `export MOD_AUDIO_STREAM_BUFFER_SECS=60`

#### Security Settings

- `MOD_AUDIO_STREAM_ALLOW_SELFSIGNED`: Allow self-signed certificates
  - Default: `false`
  - Values: `true`, `false`
  - Example: `export MOD_AUDIO_STREAM_ALLOW_SELFSIGNED=true`

- `MOD_AUDIO_STREAM_SKIP_SERVER_CERT_HOSTNAME_CHECK`: Skip hostname verification
  - Default: `false`
  - Example: `export MOD_AUDIO_STREAM_SKIP_SERVER_CERT_HOSTNAME_CHECK=true`

- `MOD_AUDIO_STREAM_ALLOW_EXPIRED`: Allow expired certificates
  - Default: `false`
  - Example: `export MOD_AUDIO_STREAM_ALLOW_EXPIRED=true`

#### Authentication Settings

- `MOD_AUDIO_STREAM_HTTP_AUTH_USER`: HTTP basic auth username
  - Example: `export MOD_AUDIO_STREAM_HTTP_AUTH_USER=myuser`

- `MOD_AUDIO_STREAM_HTTP_AUTH_PASSWORD`: HTTP basic auth password
  - Example: `export MOD_AUDIO_STREAM_HTTP_AUTH_PASSWORD=mypass`

**Example in dialplan:**
```xml
<action application="set" data="stream_auth_id=user123"/>
<action application="set" data="stream_account_id=acct456"/>
```

## WebSocket Protocol

### Message Format

The module sends JSON messages over WebSocket connections:

#### Start Message
```json
{
  "event": "start",
  "sequence": 1,
  "uuid": "session-uuid-here",
  "streamId": "my_stream_1",
  "track": "inbound",
  "codec": "L16",
  "rate": 16000,
  "metadata": {
    "custom": "data"
  }
}
```

#### Media Message
```json
{
  "event": "media",
  "sequence": 2,
  "streamId": "my_stream_1",
  "audio": "base64-encoded-audio-data"
}
```

#### Stop Message
```json
{
  "event": "stop",
  "sequence": 999,
  "streamId": "my_stream_1",
  "reason": "normal_termination"
}
```

### Bidirectional Messages

When bidirectional mode is enabled, the server can send:

#### Play Audio
```json
{
  "event": "media.play",
  "streamId": "my_stream_1",
  "payload": {
    "audioData": "base64-encoded-audio",
    "sampleRate": 16000,
    "format": "LINEAR16",
    "checkpoint": "audio_end_marker"
  }
}
```

#### Send Transcription
```json
{
  "event": "transcription.send",
  "streamId": "my_stream_1",
  "payload": {
    "text": "Hello, how can I help you today?",
    "confidence": 0.98,
    "is_final": true,
    "timestamp": 1694712000000
  }
}
```

#### Clear Audio
```json
{
  "event": "media.clear",
  "streamId": "my_stream_1"
}
```

#### Set Checkpoint
```json
{
  "event": "media.checkpoint",
  "streamId": "my_stream_1",
  "payload": {
    "name": "checkpoint_1"
  }
}
```


## Examples

### Basic Streaming

```bash
# Start streaming inbound audio at 16kHz
uuid_audio_stream ${uuid} stream1 start wss://server.com/stream inbound 16000 30 0

# Stop streaming
uuid_audio_stream ${uuid} stream1 stop
```

### Bidirectional Streaming

```bash
# Start bidirectional streaming
uuid_audio_stream ${uuid} stream1 start wss://server.com/stream both 16000 0 1 '{"mode":"interactive"}'

# Send control message
uuid_audio_stream ${uuid} stream1 send_text '{"action":"pause"}'

# Graceful shutdown
uuid_audio_stream ${uuid} stream1 graceful-shutdown "Session complete"
```

### Event Handling in Dialplan

```xml
<extension name="audio_stream_example">
  <condition field="destination_number" expression="^9999$">
    <!-- Set stream metadata -->
    <action application="set" data="stream_account_id=test_account"/>
    
    <!-- Answer the call -->
    <action application="answer"/>
    
    <!-- Start streaming -->
    <action application="uuid_audio_stream" 
            data="${uuid} demo_stream start wss://api.example.com/stream inbound 16000 30 1"/>
    
    <!-- Play a greeting -->
    <action application="playback" data="greeting.wav"/>
    
    <!-- Keep call active for streaming -->
    <action application="sleep" data="30000"/>
    
    <!-- Stop streaming -->
    <action application="uuid_audio_stream" data="${uuid} demo_stream stop"/>
  </condition>
</extension>
```

## Error Handling

### Common Error Scenarios

1. **Connection Failed**
   - Check WebSocket URL validity
   - Verify network connectivity
   - Check firewall rules

2. **Authentication Failed**
   - Verify credentials in environment variables
   - Check server authentication requirements

3. **SSL/TLS Errors**
   - Check certificate validity
   - Adjust SSL environment variables if needed

4. **Buffer Overruns**
   - Increase buffer size via `MOD_AUDIO_STREAM_BUFFER_SECS`
   - Check network latency and bandwidth

### Debugging

Enable debug logging in FreeSWITCH:

```bash
fs_cli -x "console loglevel debug"
fs_cli -x "sofia loglevel all 9"
```

Monitor events:

```bash
fs_cli -x "events plain all"
```

### Best Practices

1. **Always handle connection events** in your application
2. **Set appropriate timeouts** for your use case
3. **Monitor buffer overrun events** for performance tuning
4. **Use graceful shutdown** when possible
5. **Implement reconnection logic** in your application
6. **Validate WebSocket URLs** before starting streams
7. **Set reasonable buffer sizes** based on network conditions

## Version Information

- **Module Version**: 1.0.0
- **FreeSWITCH Compatibility**: 1.10+
- **Protocol Version**: 1.0
- **Last Updated**: 2024-01-01