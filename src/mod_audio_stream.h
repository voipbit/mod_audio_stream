// SPDX-License-Identifier: MIT
/**
 * @file mod_audio_stream.h
 * @brief VoipBit Advanced Real-Time Audio Processing Module
 *
 * This is a next-generation audio streaming module featuring:
 * - Event-driven architecture with structured messaging
 * - Adaptive quality management and connection resilience
 * - Pluggable codec and transport layer support
 * - Advanced buffer management with network adaptation
 * - Comprehensive monitoring and health reporting
 * - Machine learning ready audio processing pipeline
 *
 * @author VoipBit Engineering Team
 * @version 2.0
 * @date 2025
 * @copyright MIT License
 */
#ifndef __MOD_AUDIO_STREAM_VOIPBIT_H__
#define __MOD_AUDIO_STREAM_VOIPBIT_H__

#include <libwebsockets.h>
#include <speex/speex_resampler.h>
#include <switch.h>
#include <unistd.h>

// Always include the C wrapper interface
#include "adaptive_buffer_wrapper.h"

/**
 * @def AUDIO_STREAM_LOGGING_PREFIX
 * @brief Logging prefix used for all module log messages
 */
#ifndef AUDIO_STREAM_LOGGING_PREFIX
#define AUDIO_STREAM_LOGGING_PREFIX "mod_audio_stream"
#endif

/** @defgroup Constants Module Constants
 * @{
 */

/** @brief Media bug identifier name */
#define MEDIA_BUG_NAME "audio_stream"

/** @brief Maximum length for session identifier strings */
#define MAX_SESSION_ID_LENGTH (256)

/** @brief Maximum length for WebSocket URL strings */
#define MAX_WEBSOCKET_URL_LENGTH (512)

/** @brief Maximum length for WebSocket path strings */
#define MAX_WEBSOCKET_PATH_LENGTH (128)

/** @brief Event type for audio playback requests */
#define EVENT_PLAY_AUDIO "mod_audio_stream::media_play_start"

/** @brief Event type for audio kill/stop requests */
#define EVENT_KILL_AUDIO "mod_audio_stream::media_kill_audio"

/** @brief Event type for WebSocket disconnection */
#define EVENT_DISCONNECT "mod_audio_stream::connection_closed"

/** @brief Event type for stream start */
#define EVENT_START "mod_audio_stream::stream_started"

/** @brief Event type for stream stop */
#define EVENT_STOP "mod_audio_stream::stream_stopped"

/** @brief Event type for error conditions */
#define EVENT_ERROR "mod_audio_stream::stream_error"

/** @brief Event type for successful WebSocket connection */
#define EVENT_CONNECT_SUCCESS "mod_audio_stream::connection_established"

/** @brief Event type for failed WebSocket connection */
#define EVENT_CONNECT_FAIL "mod_audio_stream::connection_failed"

/** @brief Event type for connection timeout */
#define EVENT_CONNECT_TIMEOUT "mod_audio_stream::connection_timeout"

/** @brief Event type for degraded connection quality */
#define EVENT_CONNECT_DEGRADED "mod_audio_stream::connection_degraded"

/** @brief Event type for audio buffer overrun */
#define EVENT_BUFFER_OVERRUN "mod_audio_stream::stream_buffer_overrun"

/** @brief Event type for JSON message received */
#define EVENT_JSON "mod_audio_stream::message_received"

/** @brief Event type for stream heartbeat */
#define EVENT_STREAM_HEARTBEAT "mod_audio_stream::stream_heartbeat"

/** @brief Event type for stream timeout */
#define EVENT_STREAM_TIMEOUT "mod_audio_stream::stream_timeout"

/** @brief Event type for invalid stream input */
#define EVENT_INVALID_STREAM_INPUT "mod_audio_stream::stream_invalid_input"

/** @brief Event type for cleared audio buffer */
#define EVENT_CLEARED_AUDIO "mod_audio_stream::media_cleared"

/** @brief Event type for audio playback completion */
#define EVENT_PLAYED "mod_audio_stream::media_play_complete"

/** @brief Event type for transcription data received */
#define EVENT_TRANSCRIPTION_RECEIVED "mod_audio_stream::transcription_received"

/** @brief Stream termination reason: API request */
#define TERMINATION_REASON_API_REQUEST "API Request"

/** @brief Stream termination reason: timeout */
#define TERMINATION_REASON_STREAM_TIMEOUT "Stream Timeout"

/** @brief Stream termination reason: connection error */
#define TERMINATION_REASON_CONNECTION_ERROR "Connection error"

/** @brief Stream termination reason: call hangup */
#define TERMINATION_REASON_CALL_HANGUP "Call Hangup"

/** @brief Maximum length for metadata JSON strings */
#define MAX_METADATA_LENGTH (8192)

/** @brief VoipBit stream processing directions */
#define STREAM_DIRECTION_INBOUND 0
#define STREAM_DIRECTION_OUTBOUND 1
#define STREAM_DIRECTION_BIDIRECTIONAL 2

/** @brief VoipBit processing result codes */
#define VOIPBIT_PROCESSING_SUCCESS 0
#define VOIPBIT_PROCESSING_ERROR 1
#define VOIPBIT_PROCESSING_CIRCUIT_OPEN 2
#define VOIPBIT_PROCESSING_QUALITY_DEGRADED 3

/** @} */ // End of Constants group

/** @defgroup DataStructures Data Structures
 * @{
 */

/**
 * @brief Stream identifier structure
 *
 * Contains session and stream identifiers used for tracking individual
 * audio streaming sessions within FreeSWITCH.
 */
typedef struct stream_identifier
{
    /** @brief FreeSWITCH session UUID */
    char session_id[MAX_SESSION_ID_LENGTH];

    /** @brief Unique stream identifier */
    char stream_id[MAX_SESSION_ID_LENGTH];
} stream_identifier_t;

/**
 * @brief Audio file playout queue structure
 *
 * Maintains a linked list of audio files to be played out during
 * bidirectional streaming sessions.
 */
struct playout
{
    /** @brief Path to audio file to be played */
    char *file;

    /** @brief Pointer to next file in playout queue */
    struct playout *next;
};

/**
 * @brief Response handler function pointer type
 *
 * Function signature for handling responses and events from the WebSocket
 * connection. Used to process incoming messages and emit FreeSWITCH events.
 *
 * @param session FreeSWITCH session pointer
 * @param event_name Name of the event to be fired
 * @param json_payload JSON payload to include with the event
 */
typedef void (*response_handler_t)(switch_core_session_t *session, const char *event_name, const char *json_payload);

/**
 * @brief Stream checkpoints linked list structure
 *
 * Manages checkpoints for tracking audio playback progress during
 * bidirectional streaming sessions.
 */
typedef struct stream_checkpoints
{
    /** @brief Pointer to list head */
    struct stream_checkpoints *head;

    /** @brief Pointer to next checkpoint */
    struct stream_checkpoints *next;

    /** @brief Pointer to list tail */
    struct stream_checkpoints *tail;

    /** @brief Byte position of checkpoint in stream */
    size_t position;

    /** @brief Checkpoint name/identifier */
    char *name;
} stream_checkpoints_t;

/**
 * @brief VoipBit session context for advanced audio streaming
 *
 * Comprehensive session state management with modern patterns including
 * event-driven updates, adaptive quality control, and structured monitoring.
 * Supports multiple concurrent streams with intelligent resource management.
 */
struct voipbit_session_context
{
    /** @brief Mutex for thread-safe access to session data */
    switch_mutex_t *mutex;

    /** @brief FreeSWITCH session UUID */
    char session_id[MAX_SESSION_ID_LENGTH];

    /** @brief Unique stream identifier */
    char stream_id[MAX_SESSION_ID_LENGTH];

    /** @brief Audio track type: "inbound", "outbound", or "both" */
    char track[16];

    /** @brief Speex resampler for inbound audio */
    SpeexResamplerState *resampler;

    /** @brief Speex resampler for outbound audio */
    SpeexResamplerState *resampler_outbound;

    /** @brief Function pointer for handling responses and events */
    response_handler_t response_handler;

    /** @brief Pointer to AudioPipe instance (WebSocket client) */
    void *audio_pipe_ptr;

    /** @brief WebSocket connection state */
    int websocket_state;

    /** @brief Complete service URL including path */
    char service_url[MAX_WEBSOCKET_URL_LENGTH + MAX_WEBSOCKET_PATH_LENGTH];

    /** @brief WebSocket server hostname */
    char host[MAX_WEBSOCKET_URL_LENGTH];

    /** @brief WebSocket server port number */
    unsigned int port;

    /** @brief WebSocket path component */
    char path[MAX_WEBSOCKET_PATH_LENGTH];

    /** @brief Audio sampling rate in Hz */
    int sampling;

    /** @brief Linked list of audio files for playout */
    struct playout *playout;

    /** @brief Number of audio channels (typically 1 for mono) */
    int channels;

    /** @brief Unique numeric identifier for this session */
    unsigned int id;

    /** @brief FreeSWITCH task ID for periodic operations */
    int task_id;

    /** @brief FreeSWITCH task ID for stream timeout handling */
    int stream_timeout_task_id;

    /** @brief Flag indicating bidirectional streaming capability */
    int is_bidirectional;

    /** @brief Flag indicating if streaming has started */
    int is_started;

    /** @brief Timeout value for stream ending (seconds) */
    int stream_end_timeout;

    /** @brief Bit flag: audio streaming is paused */
    unsigned int audio_paused : 1;

    /** @brief Bit flag: graceful shutdown in progress */
    unsigned int graceful_shutdown : 1;

    /** @brief Bit flag: FreeSWITCH channel is closing */
    unsigned int channel_closing : 1;

    /** @brief Bit flag: invalid stream input notification sent */
    unsigned int invalid_stream_input_notified : 1;

    /** @brief Initial metadata JSON sent with stream start */
    char initial_metadata[MAX_METADATA_LENGTH];

    /** @brief Reason for stream termination */
    char stream_termination_reason[256];

    /** @brief Timestamp when streaming started */
    switch_time_t start_time;

    /** @brief Timestamp when streaming ended */
    switch_time_t end_time;

    /** @brief Count of audio files played */
    int play_count;

    /** @brief Write buffer for incoming audio (bidirectional mode only) */
    switch_buffer_t *write_buffer;

    /** @brief Bytes of incoming audio received */
    unsigned int stream_input_received;

    /** @brief Bytes of incoming audio played */
    unsigned int stream_input_played;

    /** @brief Total bytes available for playback */
    unsigned int total_playable_bytes;

    /** @brief Mutex for thread-safe write buffer access */
    switch_mutex_t *write_buffer_mutex;

    /** @brief Linked list of stream checkpoints */
    stream_checkpoints_t *checkpoints;

    /** @brief Flag indicating if adaptive buffering is enabled */
    int adaptive_buffer_enabled;
};

/**
 * @brief Global configuration structure
 *
 * Contains module-wide configuration settings.
 */
typedef struct globals
{
    /** @brief Memory pool for global allocations */
    switch_memory_pool_t *pool;

    /** @brief Default service URL */
    char *url;

    /** @brief Default delay in milliseconds */
    int delay;

    /** @brief Default number of retry attempts */
    int retries;

    /** @brief Default timeout in seconds */
    int timeout;

    /** @brief Default service URL (fallback) */
    char *default_url;

    /** @brief Default timeout value in seconds (fallback) */
    int default_timeout;

    /** @brief Memory pool for module allocations */
    switch_memory_pool_t *memory_pool;

    /** @brief Delay between operations in milliseconds */
    uint32_t operation_delay;

    /** @brief Number of retry attempts for operations */
    uint32_t retry_count;
} globals_t;

/** @brief Global configuration instance */
extern globals_t globals;

/** @brief Type alias for VoipBit session context structure */
typedef struct voipbit_session_context voipbit_session_context_t;

/**
 * @brief Media bug type enumeration
 *
 * Specifies the direction of audio flow for media bug attachment.
 */
typedef enum media_bug_type
{
    /** @brief Capture inbound audio (from caller) */
    MEDIA_BUG_INBOUND,

    /** @brief Capture outbound audio (to caller) */
    MEDIA_BUG_OUTBOUND
} media_bug_type_t;

/**
 * @brief VoipBit media processor callback arguments structure
 *
 * Advanced callback context with enhanced error handling, performance
 * monitoring, and adaptive processing capabilities. Supports real-time
 * audio quality adjustments and intelligent frame processing.
 */
typedef struct voipbit_media_processor_args
{
    /** @brief Thread-safe synchronization mutex */
    switch_mutex_t *sync_mutex;

    /** @brief Pointer to VoipBit session context */
    voipbit_session_context_t *session_context;

    /** @brief Audio stream processing direction */
    media_bug_type_t stream_direction;

    /** @brief Frame processing performance metrics */
    struct {
        uint64_t frames_processed;
        uint64_t frames_dropped;
        double avg_processing_time_ms;
        uint64_t last_update_timestamp;
    } performance_metrics;
} voipbit_media_processor_args_t;

/** @} */ // End of DataStructures group

/** @defgroup PublicFunctions Public Function Declarations
 * @{
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize the streaming subsystem
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t stream_init(void);

    /**
     * @brief Cleanup the streaming subsystem
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t stream_cleanup(void);

    /**
     * @brief Parse WebSocket URI into components
     *
     * @param channel FreeSWITCH channel
     * @param szServerUri URI to parse
     * @param host Output buffer for hostname
     * @param path Output buffer for path
     * @param pPort Output pointer for port number
     * @param pSslFlags Output pointer for SSL flags
     * @return 1 on success, 0 on failure
     */
    int parse_ws_uri(switch_channel_t *channel,
                     const char *szServerUri,
                     char *host,
                     char *path,
                     unsigned int *pPort,
                     int *pSslFlags);

    /**
     * @brief Stop an active audio stream
     *
     * Terminates an ongoing audio streaming session and cleans up associated
     * resources. Fires appropriate FreeSWITCH events to notify applications.
     *
     * @param session FreeSWITCH session pointer
     * @param stream_id Unique identifier for the stream to stop
     * @param reason_text Optional reason text for termination
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t do_stop(switch_core_session_t *session, char *stream_id, char *reason_text);

    /**
     * @brief Gracefully shutdown an audio stream
     *
     * Initiates a graceful shutdown of an audio streaming session, allowing
     * pending operations to complete before termination.
     *
     * @param session FreeSWITCH session pointer
     * @param stream_id Unique identifier for the stream to shutdown
     * @param reason_text Optional reason text for shutdown
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t do_graceful_shutdown(switch_core_session_t *session, char *stream_id, char *reason_text);

    /**
     * @brief VoipBit adaptive frame processor with intelligent quality control
     *
     * Advanced frame processing function with real-time quality assessment,
     * circuit breaker patterns, and adaptive buffering capabilities.
     *
     * @param session FreeSWITCH session pointer
     * @param media_processor Media bug instance
     * @param direction Stream processing direction (inbound/outbound)
     * @return SWITCH_TRUE on success, SWITCH_FALSE on failure
     */
    switch_bool_t voipbit_adaptive_frame_processor(switch_core_session_t *session,
                                                   switch_media_bug_t *media_processor,
                                                   int direction);

#ifdef __cplusplus
}
#endif

/** @} */ // End of PublicFunctions group

#endif /* __MOD_AUDIO_STREAM_VOIPBIT_H__ */
