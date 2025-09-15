// SPDX-License-Identifier: MIT
/**
 * @file adaptive_buffer_wrapper.h
 * @brief C header for adaptive buffer integration with FreeSWITCH
 */
#ifndef __ADAPTIVE_BUFFER_WRAPPER_H__
#define __ADAPTIVE_BUFFER_WRAPPER_H__

#include <switch.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Statistics structure for adaptive buffer
     */
    typedef struct adaptive_buffer_stats
    {
        uint64_t total_messages;       /**< Total messages processed */
        uint64_t dropped_messages;     /**< Number of dropped messages */
        size_t current_message_count;  /**< Current messages in buffer */
        size_t current_size_bytes;     /**< Current buffer size in bytes */
        size_t max_size_reached;       /**< Maximum buffer size reached */
        size_t recommended_size_bytes; /**< Recommended buffer size */
        uint32_t underrun_events;      /**< Number of underrun events */
        uint32_t overrun_events;       /**< Number of overrun events */
        uint32_t adaptation_events;    /**< Number of adaptation events */
        double average_latency_ms;     /**< Average latency in milliseconds */
        double current_latency_ms;     /**< Current latency in milliseconds */
        double packet_loss_rate;       /**< Packet loss rate (0.0-1.0) */
        double buffer_utilization;     /**< Buffer utilization (0.0-1.0) */
    } adaptive_buffer_stats_t;

    /**
     * @brief Initialize the adaptive buffer system
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t adaptive_buffer_init(void);

    /**
     * @brief Cleanup the adaptive buffer system
     */
    void adaptive_buffer_cleanup(void);

    /**
     * @brief Create a stream buffer
     * @param stream_id Unique stream identifier
     * @param sampling_rate Audio sampling rate
     * @param is_bidirectional Whether stream is bidirectional
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t adaptive_buffer_create_stream(const char *stream_id, int sampling_rate, int is_bidirectional);

    /**
     * @brief Destroy a stream buffer
     * @param stream_id Stream identifier to destroy
     */
    void adaptive_buffer_destroy_stream(const char *stream_id);

    /**
     * @brief Buffer an audio frame
     * @param stream_id Stream identifier
     * @param audio_data Audio data buffer
     * @param data_len Length of audio data
     * @param sequence_number Sequence number for ordering
     * @param priority Message priority (0=critical, 1=high, 2=normal, 3=low)
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t adaptive_buffer_enqueue_frame(
        const char *stream_id, const void *audio_data, size_t data_len, uint32_t sequence_number, int priority);

    /**
     * @brief Retrieve an audio frame from buffer
     * @param stream_id Stream identifier
     * @param audio_data Output buffer for audio data
     * @param data_len Pointer to receive actual data length
     * @param max_len Maximum length of output buffer
     * @param timeout_ms Timeout in milliseconds (0 = no timeout)
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t adaptive_buffer_dequeue_frame(
        const char *stream_id, void *audio_data, size_t *data_len, size_t max_len, int timeout_ms);

    /**
     * @brief Update network conditions for adaptive buffering
     * @param stream_id Stream identifier
     * @param bandwidth_kbps Available bandwidth in Kbps
     * @param latency_ms Network latency in milliseconds
     * @param packet_loss_rate Packet loss rate (0.0-1.0)
     * @param jitter_ms Network jitter in milliseconds
     */
    void adaptive_buffer_update_network(
        const char *stream_id, double bandwidth_kbps, double latency_ms, double packet_loss_rate, double jitter_ms);

    /**
     * @brief Get buffer statistics
     * @param stream_id Stream identifier
     * @param stats Pointer to statistics structure to fill
     * @return SWITCH_STATUS_SUCCESS on success, SWITCH_STATUS_FALSE on failure
     */
    switch_status_t adaptive_buffer_get_stats(const char *stream_id, adaptive_buffer_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* __ADAPTIVE_BUFFER_WRAPPER_H__ */