// SPDX-License-Identifier: MIT
/**
 * @file openai_adapter.h
 * @brief OpenAI Realtime API integration for mod_audio_stream
 * 
 * This header provides C interface for integrating OpenAI Realtime API
 * with the existing mod_audio_stream module. It handles message format
 * translation, authentication, and event processing.
 */

#ifndef __OPENAI_ADAPTER_H__
#define __OPENAI_ADAPTER_H__

#include "mod_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

// OpenAI-specific event types
#define EVENT_OPENAI_SESSION_CREATED        "mod_audio_stream::openai_session_created"
#define EVENT_OPENAI_RESPONSE_AUDIO_DELTA   "mod_audio_stream::openai_audio_delta"
#define EVENT_OPENAI_TRANSCRIPTION_DELTA    "mod_audio_stream::openai_transcription_delta"
#define EVENT_OPENAI_SPEECH_STARTED         "mod_audio_stream::openai_speech_started"
#define EVENT_OPENAI_SPEECH_STOPPED         "mod_audio_stream::openai_speech_stopped"
#define EVENT_OPENAI_ERROR                  "mod_audio_stream::openai_error"

/**
 * @brief OpenAI configuration structure
 */
typedef struct openai_config {
    char voice[32];                    // OpenAI voice (alloy, echo, fable, etc.)
    char instructions[1024];           // System instructions
    char input_audio_format[32];       // Audio format (pcm16)
    char output_audio_format[32];      // Audio format (pcm16)
    int turn_detection_enabled;        // Server-side VAD
    double turn_detection_threshold;   // VAD threshold
    int turn_detection_prefix_padding_ms;    // VAD padding
    int turn_detection_silence_duration_ms;  // VAD silence duration
    int input_audio_transcription_enabled;   // Enable transcription
    char transcription_model[64];      // Transcription model
} openai_config_t;

/**
 * @brief Create default OpenAI configuration
 * 
 * @param voice Voice name (alloy, echo, fable, onyx, nova, shimmer)
 * @param instructions System instructions for the AI
 * @return Pointer to allocated config structure
 */
openai_config_t* openai_create_default_config(const char* voice, const char* instructions);

/**
 * @brief Free OpenAI configuration
 * 
 * @param config Configuration to free
 */
void openai_free_config(openai_config_t* config);

/**
 * @brief Get OpenAI Realtime WebSocket URL
 * 
 * @return Allocated string with WebSocket URL (caller must free)
 */
char* openai_get_websocket_url(void);

/**
 * @brief Generate OpenAI session.update message
 * 
 * @param config OpenAI configuration
 * @return Allocated JSON string (caller must free)
 */
char* openai_generate_session_update(const openai_config_t* config);

/**
 * @brief Generate input_audio_buffer.append message
 * 
 * @param base64_audio Base64-encoded audio data
 * @return Allocated JSON string (caller must free)
 */
char* openai_generate_input_audio_buffer_append(const char* base64_audio);

/**
 * @brief Generate input_audio_buffer.commit message
 * 
 * @return Allocated JSON string (caller must free)
 */
char* openai_generate_input_audio_buffer_commit(void);

/**
 * @brief Parse OpenAI event message and handle appropriately
 * 
 * @param session FreeSWITCH session
 * @param stream_id Stream identifier
 * @param json_message JSON message from OpenAI
 * @return SWITCH_STATUS_SUCCESS on success
 */
switch_status_t openai_handle_message(switch_core_session_t* session, 
                                     const char* stream_id, 
                                     const char* json_message);

/**
 * @brief Check if a WebSocket URL is for OpenAI Realtime API
 * 
 * @param url WebSocket URL to check
 * @return 1 if OpenAI URL, 0 otherwise
 */
int openai_is_realtime_url(const char* url);

/**
 * @brief Transform regular audio stream message to OpenAI format
 * 
 * @param original_message Original mod_audio_stream message
 * @param stream_id Stream identifier
 * @param is_openai_mode 1 if in OpenAI mode, 0 otherwise
 * @return Transformed message (caller must free) or NULL if no transformation needed
 */
char* openai_transform_outgoing_message(const char* original_message, 
                                       const char* stream_id, 
                                       int is_openai_mode);

#ifdef __cplusplus
}
#endif

#endif // __OPENAI_ADAPTER_H__