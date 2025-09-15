// SPDX-License-Identifier: MIT
/**
 * @file lws_glue.h
 * @brief WebSocket integration layer for mod_audio_stream
 *
 * This header provides the interface between the FreeSWITCH module and
 * the libwebsockets-based audio streaming implementation. It defines
 * functions for WebSocket session management, audio frame processing,
 * and connection lifecycle management.
 *
 * @author FreeSWITCH Community
 * @version 1.0
 * @date 2024
 */
#ifndef __LWS_GLUE_H__
#define __LWS_GLUE_H__

#include "mod_audio_stream.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Parse WebSocket URI into components
     *
     * Parses a WebSocket URI string into its constituent components including
     * hostname, port, path, and SSL flags. Validates the URI format and
     * extracts connection parameters needed for WebSocket client setup.
     *
     * @param channel FreeSWITCH channel for logging context
     * @param websocket_uri Complete WebSocket URI to parse
     * @param hostname_buffer Buffer to store extracted hostname
     * @param path_buffer Buffer to store extracted path
     * @param port_ptr Pointer to store extracted port number
     * @param ssl_flags_ptr Pointer to store SSL/TLS flags
     * @return 0 on success, non-zero on parsing error
     */
    int parse_websocket_uri(switch_channel_t *channel,
                            const char *websocket_uri,
                            char *hostname_buffer,
                            char *path_buffer,
                            unsigned int *port_ptr,
                            int *ssl_flags_ptr);

    /**
     * @brief Initialize the streaming subsystem
     *
     * Initializes global resources for the audio streaming subsystem including
     * WebSocket contexts, thread pools, and internal data structures.
     * Must be called during module load.
     *
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t stream_subsystem_initialize(void);

    /**
     * @brief Cleanup the streaming subsystem
     *
     * Cleans up all global resources allocated by the streaming subsystem.
     * Must be called during module unload to prevent resource leaks.
     *
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t stream_subsystem_cleanup(void);
    switch_status_t stream_session_init(switch_core_session_t *session,
                                        char *stream_id,
                                        char *service_url,
                                        response_handler_t response_handler,
                                        uint32_t samples_per_second,
                                        char *host,
                                        unsigned int port,
                                        char *path,
                                        char *codec,
                                        int sampling,
                                        int sslFlags,
                                        int channels,
                                        char *track,
                                        int is_bidirectional,
                                        int timeout,
                                        char *metadata,
                                        void **ppUserData);
    switch_status_t stream_session_cleanup(
        switch_core_session_t *session, char *stream_id, char *text, int channelIsClosing, int zeroBilling);
    switch_status_t stream_session_pauseresume(switch_core_session_t *session, char *stream_id, int pause);
    switch_status_t stream_session_graceful_shutdown(switch_core_session_t *session, char *stream_id);
    switch_status_t stream_session_send_text(switch_core_session_t *session, char *stream_id, char *text);
    switch_bool_t stream_frame(switch_core_session_t *session, switch_media_bug_t *bug);
    switch_status_t stream_service_threads();
    switch_status_t stream_ws_close_connection(private_data_t *tech_pvt);
    switch_status_t stream_ws_send_played_event(private_data_t *tech_pvt, const char *data);
#ifdef __cplusplus
}
#endif
#endif
