// SPDX-License-Identifier: MIT
/**
 * @file advanced_api.h
 * @brief Advanced API interface for mod_audio_stream
 *
 * This header provides a comprehensive API interface with advanced
 * commands, JSON-based configuration, and detailed response formats.
 *
 * @author FreeSWITCH Community
 * @version 2.0
 * @date 2024
 */
#ifndef __ADVANCED_API_H__
#define __ADVANCED_API_H__

#include "mod_audio_stream.h"
#include <switch.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @defgroup AdvancedAPI Advanced API Interface
 * @{
 */

/**
 * @brief Extended API command definitions
 */
#define STREAM_API_SYNTAX_V2                                                                                           \
    "<uuid> <command> [options...]\n"                                                                                  \
    "\nCommands:\n"                                                                                                    \
    "  start <stream_id> <profile> <url> [metadata]\n"                                                                 \
    "  stop <stream_id> [reason]\n"                                                                                    \
    "  pause <stream_id>\n"                                                                                            \
    "  resume <stream_id>\n"                                                                                           \
    "  send_text <stream_id> <text>\n"                                                                                 \
    "  graceful_shutdown <stream_id> [reason]\n"                                                                       \
    "  get_status <stream_id>\n"                                                                                       \
    "  list_streams\n"                                                                                                 \
    "  get_metrics [stream_id]\n"                                                                                      \
    "  reload_config\n"                                                                                                \
    "  health_check\n"                                                                                                 \
    "  plugin_list\n"                                                                                                  \
    "  plugin_enable <plugin_name>\n"                                                                                  \
    "  plugin_disable <plugin_name>\n"                                                                                 \
    "  plugin_config <plugin_name> <json_config>\n"                                                                    \
    "  codec_list\n"                                                                                                   \
    "  profile_list\n"                                                                                                 \
    "  debug_level <level>\n"                                                                                          \
    "\nExamples:\n"                                                                                                    \
    "  uuid_audio_stream_v2 <uuid> start transcribe_stream transcription wss://api.example.com/v1/stream\n"            \
    "  uuid_audio_stream_v2 <uuid> get_status transcribe_stream\n"                                                     \
    "  uuid_audio_stream_v2 <uuid> get_metrics\n"                                                                      \
    "  uuid_audio_stream_v2 <uuid> plugin_config noise_reducer '{\"level\": 0.8}'"

    /**
     * @brief API response status codes
     */
    typedef enum api_response_status
    {
        API_SUCCESS = 0,
        API_ERROR_INVALID_COMMAND,
        API_ERROR_INVALID_ARGUMENTS,
        API_ERROR_SESSION_NOT_FOUND,
        API_ERROR_STREAM_NOT_FOUND,
        API_ERROR_STREAM_ALREADY_EXISTS,
        API_ERROR_PLUGIN_NOT_FOUND,
        API_ERROR_CONFIGURATION_ERROR,
        API_ERROR_PERMISSION_DENIED,
        API_ERROR_RESOURCE_EXHAUSTED,
        API_ERROR_INTERNAL_ERROR
    } api_response_status_t;

    /**
     * @brief Stream status information
     */
    typedef struct stream_status
    {
        char stream_id[MAX_SESSION_ID_LENGTH];
        char profile_name[64];
        char codec[32];
        char server_url[MAX_WEBSOCKET_URL_LENGTH];
        char state[32]; // "connecting", "connected", "streaming", "paused", "error", "closed"
        switch_time_t start_time;
        switch_time_t last_activity;
        uint64_t frames_sent;
        uint64_t frames_received;
        uint64_t bytes_sent;
        uint64_t bytes_received;
        double average_latency_ms;
        double current_latency_ms;
        double packet_loss_rate;
        double audio_quality_score;
        int reconnection_count;
        char last_error[256];
        bool is_bidirectional;
        uint32_t sample_rate;
        uint16_t channels;
    } stream_status_t;

    /**
     * @brief System health information
     */
    typedef struct system_health
    {
        char overall_status[32]; // "healthy", "degraded", "unhealthy"
        uint32_t active_streams;
        uint32_t total_streams;
        uint32_t failed_streams;
        double cpu_usage_percent;
        double memory_usage_mb;
        double disk_usage_percent;
        uint32_t plugin_count;
        uint32_t enabled_plugins;
        switch_time_t uptime;
        switch_time_t last_check;
    } system_health_t;

    /**
     * @brief Plugin information for API responses
     */
    typedef struct api_plugin_info
    {
        char name[128];
        char version[32];
        char description[256];
        char type[32];
        char status[32]; // "loaded", "enabled", "disabled", "error"
        bool is_enabled;
        uint64_t executions;
        double average_execution_time_ms;
        char last_error[256];
    } api_plugin_info_t;

    /**
     * @brief Codec information for API responses
     */
    typedef struct api_codec_info
    {
        char name[32];
        char description[128];
        bool is_available;
        uint32_t sample_rates[16];
        uint32_t sample_rate_count;
        uint16_t supported_channels;
        bool supports_variable_bitrate;
        uint32_t default_bitrate;
        char quality_levels[256]; // JSON array of supported quality levels
    } api_codec_info_t;

    /**
     * @brief Configuration profile information
     */
    typedef struct api_profile_info
    {
        char name[64];
        char description[256];
        char codec[32];
        uint32_t sample_rate;
        uint16_t channels;
        bool is_default;
        char server_urls[1024];       // JSON array of server URLs
        char processing_options[512]; // JSON object of processing options
    } api_profile_info_t;

    /**
     * @brief API response structure
     */
    typedef struct api_response
    {
        api_response_status_t status;
        char message[512];
        char data[4096]; // JSON formatted response data
        switch_time_t timestamp;
    } api_response_t;

    /**
     * @brief Advanced stream start parameters
     */
    typedef struct stream_start_params
    {
        char stream_id[MAX_SESSION_ID_LENGTH];
        char profile_name[64];
        char server_url[MAX_WEBSOCKET_URL_LENGTH];
        char metadata[2048];       // JSON metadata
        char custom_headers[1024]; // JSON custom headers
        int timeout_seconds;
        bool force_reconnect;
        char client_id[128];
        char auth_token[512];
    } stream_start_params_t;

    /**
     * @brief Metrics query parameters
     */
    typedef struct metrics_query_params
    {
        char stream_id[MAX_SESSION_ID_LENGTH]; // Empty for global metrics
        char metric_names[512];                // Comma-separated list, empty for all
        char time_range[32];                   // "1h", "24h", "7d", etc.
        char format[16];                       // "json", "prometheus", "csv"
        bool include_labels;
        bool aggregate_data;
    } metrics_query_params_t;

    /**
     * @brief Main advanced API function
     */
    SWITCH_STANDARD_API(stream_function_v2);

    /**
     * @brief Command handler functions
     */
    api_response_t handle_start_command(switch_core_session_t *session, const stream_start_params_t *params);
    api_response_t handle_stop_command(switch_core_session_t *session, const char *stream_id, const char *reason);
    api_response_t handle_pause_command(switch_core_session_t *session, const char *stream_id);
    api_response_t handle_resume_command(switch_core_session_t *session, const char *stream_id);
    api_response_t handle_send_text_command(switch_core_session_t *session, const char *stream_id, const char *text);
    api_response_t
    handle_graceful_shutdown_command(switch_core_session_t *session, const char *stream_id, const char *reason);
    api_response_t handle_get_status_command(switch_core_session_t *session, const char *stream_id);
    api_response_t handle_list_streams_command(switch_core_session_t *session);
    api_response_t handle_get_metrics_command(const metrics_query_params_t *params);
    api_response_t handle_reload_config_command(void);
    api_response_t handle_health_check_command(void);
    api_response_t handle_plugin_list_command(void);
    api_response_t handle_plugin_enable_command(const char *plugin_name);
    api_response_t handle_plugin_disable_command(const char *plugin_name);
    api_response_t handle_plugin_config_command(const char *plugin_name, const char *json_config);
    api_response_t handle_codec_list_command(void);
    api_response_t handle_profile_list_command(void);
    api_response_t handle_debug_level_command(const char *level);

    /**
     * @brief Utility functions for API responses
     */
    void format_api_response(api_response_t *response,
                             api_response_status_t status,
                             const char *message,
                             const char *data_json);
    char *serialize_stream_status(const stream_status_t *status);
    char *serialize_system_health(const system_health_t *health);
    char *serialize_plugin_info_list(const api_plugin_info_t *plugins, size_t count);
    char *serialize_codec_info_list(const api_codec_info_t *codecs, size_t count);
    char *serialize_profile_info_list(const api_profile_info_t *profiles, size_t count);

    /**
     * @brief Parameter parsing utilities
     */
    bool parse_stream_start_params(const char *args[], int argc, stream_start_params_t *params);
    bool parse_metrics_query_params(const char *args[], int argc, metrics_query_params_t *params);
    bool validate_stream_id(const char *stream_id);
    bool validate_profile_name(const char *profile_name);
    bool validate_json_string(const char *json_str);

    /**
     * @brief Enhanced event firing functions
     */
    void fire_stream_event_v2(switch_core_session_t *session,
                              const char *event_name,
                              const char *stream_id,
                              const char *json_data);
    void fire_system_event_v2(const char *event_name, const char *json_data);

    /**
     * @brief Status and health monitoring functions
     */
    stream_status_t *get_stream_status(switch_core_session_t *session, const char *stream_id);
    system_health_t get_system_health(void);
    bool is_stream_active(switch_core_session_t *session, const char *stream_id);
    uint32_t get_active_stream_count(void);
    char *get_stream_list_json(switch_core_session_t *session);

    /**
     * @brief Configuration and profile management
     */
    bool reload_module_configuration(void);
    bool validate_profile_configuration(const char *profile_name);
    char *get_profile_configuration_json(const char *profile_name);
    bool set_profile_configuration(const char *profile_name, const char *json_config);
    char *get_available_profiles_json(void);

    /**
     * @brief Real-time command interface
     */
    typedef struct rtc_command
    {
        char command[64];
        char stream_id[MAX_SESSION_ID_LENGTH];
        char parameters[1024];
        switch_time_t timestamp;
        char session_uuid[SWITCH_UUID_FORMATTED_LENGTH + 1];
    } rtc_command_t;

    /**
     * @brief Real-time command processing
     */
    bool process_realtime_command(const rtc_command_t *command, api_response_t *response);
    bool start_realtime_command_listener(uint16_t port);
    void stop_realtime_command_listener(void);

    /**
     * @brief Batch operations
     */
    typedef struct batch_operation
    {
        char operation_id[64];
        char commands[4096]; // JSON array of commands
        switch_time_t created;
        switch_time_t completed;
        uint32_t total_commands;
        uint32_t completed_commands;
        uint32_t failed_commands;
        char status[32];    // "pending", "running", "completed", "failed"
        char results[8192]; // JSON array of results
    } batch_operation_t;

    bool submit_batch_operation(const char *commands_json, char *operation_id, size_t id_size);
    batch_operation_t *get_batch_operation_status(const char *operation_id);
    char *list_batch_operations_json(void);

    /**
     * @brief WebSocket API for real-time monitoring
     */
    bool start_websocket_api_server(uint16_t port);
    void stop_websocket_api_server(void);
    bool register_websocket_client(const char *client_id, const char *subscription_filter);
    bool unregister_websocket_client(const char *client_id);
    void broadcast_to_websocket_clients(const char *message_type, const char *json_data);

    /**
     * @brief Export and import functions
     */
    char *export_configuration_json(void);
    bool import_configuration_json(const char *json_config);
    char *export_stream_statistics_csv(const char *time_range);
    char *export_plugin_statistics_json(void);

    /**
     * @brief Debugging and diagnostics
     */
    typedef enum debug_level
    {
        DEBUG_LEVEL_OFF = 0,
        DEBUG_LEVEL_ERROR,
        DEBUG_LEVEL_WARN,
        DEBUG_LEVEL_INFO,
        DEBUG_LEVEL_DEBUG,
        DEBUG_LEVEL_TRACE
    } debug_level_t;

    bool set_debug_level(debug_level_t level);
    debug_level_t get_debug_level(void);
    char *get_debug_log_json(const char *stream_id, uint32_t line_count);
    bool enable_packet_capture(const char *stream_id, const char *output_file);
    bool disable_packet_capture(const char *stream_id);

    /** @} */ // End of AdvancedAPI group

#ifdef __cplusplus
}
#endif

#endif /* __ADVANCED_API_H__ */