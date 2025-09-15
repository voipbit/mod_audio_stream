// SPDX-License-Identifier: MIT
#include "openai_adapter.h"
#include "base64.hpp"
#include <switch_json.h>
#include <stdlib.h>
#include <string.h>

// C++ wrapper for base64 functions
extern "C" {
    static char* base64_encode_wrapper(const unsigned char* data, size_t len) {
        std::string encoded = base64::base64_encode(data, len);
        char* result = (char*)malloc(encoded.length() + 1);
        if (result) {
            strcpy(result, encoded.c_str());
        }
        return result;
    }
    
    static char* base64_decode_wrapper(const char* encoded) {
        std::string decoded = base64::base64_decode(std::string(encoded));
        char* result = (char*)malloc(decoded.length() + 1);
        if (result) {
            memcpy(result, decoded.c_str(), decoded.length());
            result[decoded.length()] = '\0';
        }
        return result;
    }
}

openai_config_t* openai_create_default_config(const char* voice, const char* instructions)
{
    openai_config_t* config = (openai_config_t*)malloc(sizeof(openai_config_t));
    if (!config) return NULL;
    
    // Set default values
    strncpy(config->voice, voice ? voice : "alloy", sizeof(config->voice) - 1);
    config->voice[sizeof(config->voice) - 1] = '\0';
    
    strncpy(config->instructions, instructions ? instructions : "You are a helpful voice assistant.", sizeof(config->instructions) - 1);
    config->instructions[sizeof(config->instructions) - 1] = '\0';
    
    strncpy(config->input_audio_format, "pcm16", sizeof(config->input_audio_format) - 1);
    config->input_audio_format[sizeof(config->input_audio_format) - 1] = '\0';
    
    strncpy(config->output_audio_format, "pcm16", sizeof(config->output_audio_format) - 1);
    config->output_audio_format[sizeof(config->output_audio_format) - 1] = '\0';
    
    config->turn_detection_enabled = 1;
    config->turn_detection_threshold = 0.5;
    config->turn_detection_prefix_padding_ms = 300;
    config->turn_detection_silence_duration_ms = 200;
    config->input_audio_transcription_enabled = 0;
    
    strncpy(config->transcription_model, "whisper-1", sizeof(config->transcription_model) - 1);
    config->transcription_model[sizeof(config->transcription_model) - 1] = '\0';
    
    return config;
}

void openai_free_config(openai_config_t* config)
{
    if (config) {
        free(config);
    }
}

char* openai_get_websocket_url(void)
{
    const char* model = getenv("OPENAI_REALTIME_MODEL");
    if (!model) {
        model = "gpt-4o-realtime-preview-2024-10-01";
    }
    
    char* url = (char*)malloc(512);
    if (url) {
        snprintf(url, 512, "wss://api.openai.com/v1/realtime?model=%s", model);
    }
    
    return url;
}

char* openai_generate_session_update(const openai_config_t* config)
{
    if (!config) return NULL;
    
    cJSON* root = cJSON_CreateObject();
    cJSON* session = cJSON_CreateObject();
    cJSON* modalities = cJSON_CreateArray();
    
    // Main event structure
    cJSON_AddStringToObject(root, "type", "session.update");
    
    // Session configuration
    cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
    cJSON_AddItemToObject(session, "modalities", modalities);
    
    cJSON_AddStringToObject(session, "instructions", config->instructions);
    cJSON_AddStringToObject(session, "voice", config->voice);
    cJSON_AddStringToObject(session, "input_audio_format", config->input_audio_format);
    cJSON_AddStringToObject(session, "output_audio_format", config->output_audio_format);
    
    // Turn detection configuration
    if (config->turn_detection_enabled) {
        cJSON* turn_detection = cJSON_CreateObject();
        cJSON_AddStringToObject(turn_detection, "type", "server_vad");
        cJSON_AddNumberToObject(turn_detection, "threshold", config->turn_detection_threshold);
        cJSON_AddNumberToObject(turn_detection, "prefix_padding_ms", config->turn_detection_prefix_padding_ms);
        cJSON_AddNumberToObject(turn_detection, "silence_duration_ms", config->turn_detection_silence_duration_ms);
        cJSON_AddItemToObject(session, "turn_detection", turn_detection);
    }
    
    // Audio transcription configuration
    if (config->input_audio_transcription_enabled) {
        cJSON* input_audio_transcription = cJSON_CreateObject();
        cJSON_AddStringToObject(input_audio_transcription, "model", config->transcription_model);
        cJSON_AddItemToObject(session, "input_audio_transcription", input_audio_transcription);
    }
    
    cJSON_AddItemToObject(root, "session", session);
    
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_string;
}

char* openai_generate_input_audio_buffer_append(const char* base64_audio)
{
    if (!base64_audio) return NULL;
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
    cJSON_AddStringToObject(root, "audio", base64_audio);
    
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_string;
}

char* openai_generate_input_audio_buffer_commit(void)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.commit");
    
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_string;
}

switch_status_t openai_handle_message(switch_core_session_t* session, 
                                     const char* stream_id, 
                                     const char* json_message)
{
    if (!session || !stream_id || !json_message) {
        return SWITCH_STATUS_FALSE;
    }
    
    cJSON* root = cJSON_Parse(json_message);
    if (!root) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                         "OpenAI: Failed to parse JSON message\n");
        return SWITCH_STATUS_FALSE;
    }
    
    cJSON* type_item = cJSON_GetObjectItem(root, "type");
    if (!type_item || !cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return SWITCH_STATUS_FALSE;
    }
    
    const char* event_type = cJSON_GetStringValue(type_item);
    switch_channel_t* channel = switch_core_session_get_channel(session);
    
    if (strcmp(event_type, "session.created") == 0) {
        // Handle session created
        switch_event_t* event;
        switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_SESSION_CREATED);
        switch_channel_event_set_data(channel, event);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
        switch_event_add_body(event, "%s", json_message);
        switch_event_fire(&event);
        
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                         "OpenAI session created for stream %s\n", stream_id);
                         
    } else if (strcmp(event_type, "response.audio.delta") == 0) {
        // Handle audio delta
        cJSON* delta_item = cJSON_GetObjectItem(root, "delta");
        if (delta_item && cJSON_IsString(delta_item)) {
            const char* base64_audio = cJSON_GetStringValue(delta_item);
            
            switch_event_t* event;
            switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_RESPONSE_AUDIO_DELTA);
            switch_channel_event_set_data(channel, event);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "audio_format", "pcm16");
            switch_event_add_body(event, "%s", base64_audio);
            switch_event_fire(&event);
            
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                             "OpenAI audio delta received for stream %s\n", stream_id);
        }
        
    } else if (strcmp(event_type, "response.audio_transcript.delta") == 0) {
        // Handle transcription delta
        cJSON* delta_item = cJSON_GetObjectItem(root, "delta");
        if (delta_item && cJSON_IsString(delta_item)) {
            const char* transcript_delta = cJSON_GetStringValue(delta_item);
            
            switch_event_t* event;
            switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_TRANSCRIPTION_DELTA);
            switch_channel_event_set_data(channel, event);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcript_delta", transcript_delta);
            
            cJSON* body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "stream_id", stream_id);
            cJSON_AddStringToObject(body, "delta", transcript_delta);
            char* body_str = cJSON_Print(body);
            if (body_str) {
                switch_event_add_body(event, "%s", body_str);
                free(body_str);
            }
            cJSON_Delete(body);
            
            switch_event_fire(&event);
            
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                             "OpenAI transcript delta: %s\n", transcript_delta);
        }
        
    } else if (strcmp(event_type, "input_audio_buffer.speech_started") == 0) {
        // Handle speech started
        switch_event_t* event;
        switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_SPEECH_STARTED);
        switch_channel_event_set_data(channel, event);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
        switch_event_fire(&event);
        
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                         "OpenAI detected speech started\n");
                         
    } else if (strcmp(event_type, "input_audio_buffer.speech_stopped") == 0) {
        // Handle speech stopped
        switch_event_t* event;
        switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_SPEECH_STOPPED);
        switch_channel_event_set_data(channel, event);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
        switch_event_fire(&event);
        
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                         "OpenAI detected speech stopped\n");
                         
    } else if (strcmp(event_type, "error") == 0) {
        // Handle error
        cJSON* error_obj = cJSON_GetObjectItem(root, "error");
        if (error_obj) {
            cJSON* message_item = cJSON_GetObjectItem(error_obj, "message");
            const char* error_message = (message_item && cJSON_IsString(message_item)) ? 
                                       cJSON_GetStringValue(message_item) : "Unknown error";
            
            switch_event_t* event;
            switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, EVENT_OPENAI_ERROR);
            switch_channel_event_set_data(channel, event);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", stream_id);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "error_message", error_message);
            
            char* error_json = cJSON_Print(error_obj);
            if (error_json) {
                switch_event_add_body(event, "%s", error_json);
                free(error_json);
            }
            
            switch_event_fire(&event);
            
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                             "OpenAI error: %s\n", error_message);
        }
    }
    
    cJSON_Delete(root);
    return SWITCH_STATUS_SUCCESS;
}

int openai_is_realtime_url(const char* url)
{
    if (!url) return 0;
    return (strstr(url, "api.openai.com/v1/realtime") != NULL);
}

char* openai_transform_outgoing_message(const char* original_message, 
                                       const char* stream_id, 
                                       int is_openai_mode)
{
    if (!is_openai_mode || !original_message || !stream_id) {
        return NULL; // No transformation needed
    }
    
    // Parse the original message
    cJSON* root = cJSON_Parse(original_message);
    if (!root) return NULL;
    
    cJSON* event_item = cJSON_GetObjectItem(root, "event");
    if (!event_item || !cJSON_IsString(event_item)) {
        cJSON_Delete(root);
        return NULL;
    }
    
    const char* event_type = cJSON_GetStringValue(event_item);
    char* result = NULL;
    
    if (strcmp(event_type, "start") == 0) {
        // Don't transform start events - they're handled by the OpenAI adapter
        result = NULL;
        
    } else if (strcmp(event_type, "media") == 0) {
        // Transform media events to OpenAI input_audio_buffer.append
        cJSON* media_obj = cJSON_GetObjectItem(root, "media");
        if (media_obj) {
            cJSON* payload_item = cJSON_GetObjectItem(media_obj, "payload");
            if (payload_item && cJSON_IsString(payload_item)) {
                const char* base64_audio = cJSON_GetStringValue(payload_item);
                result = openai_generate_input_audio_buffer_append(base64_audio);
            }
        }
        
    } else if (strcmp(event_type, "stop") == 0) {
        // Don't transform stop events
        result = NULL;
    }
    
    cJSON_Delete(root);
    return result;
}