// SPDX-License-Identifier: MIT
/**
 * @file adaptive_buffer_wrapper.cpp
 * @brief C wrapper for adaptive buffer integration with FreeSWITCH
 */
#include "adaptive_buffer_wrapper.h"
#include "adaptive_buffer.hpp"
#include <switch.h>

// Global adaptive buffer manager instance
static AdaptiveBufferManager *g_adaptive_buffer_manager = nullptr;

extern "C"
{

    /**
     * @brief Initialize the adaptive buffer system
     */
    switch_status_t adaptive_buffer_init(void)
    {
        if (g_adaptive_buffer_manager != nullptr)
        {
            return SWITCH_STATUS_SUCCESS; // Already initialized
        }

        try
        {
            g_adaptive_buffer_manager = new AdaptiveBufferManager();

            // Initialize with balanced configuration
            buffer_config_t config = BufferConfigurations::Balanced;
            if (!g_adaptive_buffer_manager->initialize(config))
            {
                delete g_adaptive_buffer_manager;
                g_adaptive_buffer_manager = nullptr;
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_ERROR,
                                  "mod_audio_stream: Failed to initialize adaptive buffer manager\n");
                return SWITCH_STATUS_FALSE;
            }

            // Start monitoring thread
            if (!g_adaptive_buffer_manager->start_monitoring())
            {
                delete g_adaptive_buffer_manager;
                g_adaptive_buffer_manager = nullptr;
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_ERROR,
                                  "mod_audio_stream: Failed to start adaptive buffer monitoring\n");
                return SWITCH_STATUS_FALSE;
            }

            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_INFO,
                              "mod_audio_stream: Adaptive buffer system initialized successfully\n");
            return SWITCH_STATUS_SUCCESS;
        }
        catch (const std::exception &e)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream: Exception initializing adaptive buffer: %s\n",
                              e.what());
            if (g_adaptive_buffer_manager)
            {
                delete g_adaptive_buffer_manager;
                g_adaptive_buffer_manager = nullptr;
            }
            return SWITCH_STATUS_FALSE;
        }
    }

    /**
     * @brief Cleanup the adaptive buffer system
     */
    void adaptive_buffer_cleanup(void)
    {
        if (g_adaptive_buffer_manager)
        {
            try
            {
                g_adaptive_buffer_manager->stop_monitoring();
                delete g_adaptive_buffer_manager;
                g_adaptive_buffer_manager = nullptr;
                switch_log_printf(
                    SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod_audio_stream: Adaptive buffer system cleaned up\n");
            }
            catch (const std::exception &e)
            {
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_ERROR,
                                  "mod_audio_stream: Exception during adaptive buffer cleanup: %s\n",
                                  e.what());
            }
        }
    }

    /**
     * @brief Create a stream buffer
     */
    switch_status_t adaptive_buffer_create_stream(const char *stream_id, int sampling_rate, int is_bidirectional)
    {
        if (!g_adaptive_buffer_manager || !stream_id)
        {
            return SWITCH_STATUS_FALSE;
        }

        try
        {
            // Choose configuration based on sampling rate and bidirectional mode
            buffer_config_t config;
            if (is_bidirectional)
            {
                if (sampling_rate >= 16000)
                {
                    config = BufferConfigurations::HighQuality;
                }
                else
                {
                    config = BufferConfigurations::Balanced;
                }
            }
            else
            {
                if (sampling_rate >= 16000)
                {
                    config = BufferConfigurations::Balanced;
                }
                else
                {
                    config = BufferConfigurations::LowLatency;
                }
            }

            if (!g_adaptive_buffer_manager->create_buffer(std::string(stream_id), config))
            {
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_ERROR,
                                  "mod_audio_stream: Failed to create buffer for stream %s\n",
                                  stream_id);
                return SWITCH_STATUS_FALSE;
            }

            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_DEBUG,
                              "mod_audio_stream: Created adaptive buffer for stream %s (rate=%d, bidirectional=%d)\n",
                              stream_id,
                              sampling_rate,
                              is_bidirectional);
            return SWITCH_STATUS_SUCCESS;
        }
        catch (const std::exception &e)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream: Exception creating stream buffer: %s\n",
                              e.what());
            return SWITCH_STATUS_FALSE;
        }
    }

    /**
     * @brief Destroy a stream buffer
     */
    void adaptive_buffer_destroy_stream(const char *stream_id)
    {
        if (!g_adaptive_buffer_manager || !stream_id)
        {
            return;
        }

        try
        {
            if (g_adaptive_buffer_manager->destroy_buffer(std::string(stream_id)))
            {
                switch_log_printf(SWITCH_CHANNEL_LOG,
                                  SWITCH_LOG_DEBUG,
                                  "mod_audio_stream: Destroyed adaptive buffer for stream %s\n",
                                  stream_id);
            }
        }
        catch (const std::exception &e)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream: Exception destroying stream buffer: %s\n",
                              e.what());
        }
    }

    /**
     * @brief Buffer an audio frame
     */
    switch_status_t adaptive_buffer_enqueue_frame(
        const char *stream_id, const void *audio_data, size_t data_len, uint32_t sequence_number, int priority)
    {
        if (!g_adaptive_buffer_manager || !stream_id || !audio_data || data_len == 0)
        {
            return SWITCH_STATUS_FALSE;
        }

        try
        {
            // Create buffered message
            buffered_message_t msg;
            msg.data.resize(data_len);
            memcpy(msg.data.data(), audio_data, data_len);
            msg.sequence_number = sequence_number;
            msg.timestamp = std::chrono::system_clock::now();
            msg.deadline = msg.timestamp + std::chrono::milliseconds(5000); // 5 second deadline

            // Map priority
            switch (priority)
            {
                case 0:
                    msg.priority = PRIORITY_CRITICAL;
                    break;
                case 1:
                    msg.priority = PRIORITY_HIGH;
                    break;
                case 2:
                    msg.priority = PRIORITY_NORMAL;
                    break;
                default:
                    msg.priority = PRIORITY_LOW;
                    break;
            }

            if (!g_adaptive_buffer_manager->enqueue_message(std::string(stream_id), msg))
            {
                return SWITCH_STATUS_FALSE;
            }

            return SWITCH_STATUS_SUCCESS;
        }
        catch (const std::exception &e)
        {
            switch_log_printf(
                SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mod_audio_stream: Exception enqueueing frame: %s\n", e.what());
            return SWITCH_STATUS_FALSE;
        }
    }

    /**
     * @brief Retrieve an audio frame from buffer
     */
    switch_status_t adaptive_buffer_dequeue_frame(
        const char *stream_id, void *audio_data, size_t *data_len, size_t max_len, int timeout_ms)
    {
        if (!g_adaptive_buffer_manager || !stream_id || !audio_data || !data_len)
        {
            return SWITCH_STATUS_FALSE;
        }

        try
        {
            buffered_message_t msg;
            std::chrono::milliseconds timeout(timeout_ms);

            if (!g_adaptive_buffer_manager->dequeue_message(std::string(stream_id), msg, timeout))
            {
                *data_len = 0;
                return SWITCH_STATUS_FALSE;
            }

            // Copy data to output buffer
            size_t copy_len = std::min(msg.data.size(), max_len);
            memcpy(audio_data, msg.data.data(), copy_len);
            *data_len = copy_len;

            return SWITCH_STATUS_SUCCESS;
        }
        catch (const std::exception &e)
        {
            switch_log_printf(
                SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mod_audio_stream: Exception dequeuing frame: %s\n", e.what());
            return SWITCH_STATUS_FALSE;
        }
    }

    /**
     * @brief Update network conditions for adaptive buffering
     */
    void adaptive_buffer_update_network(
        const char *stream_id, double bandwidth_kbps, double latency_ms, double packet_loss_rate, double jitter_ms)
    {
        if (!g_adaptive_buffer_manager || !stream_id)
        {
            return;
        }

        try
        {
            network_condition_t condition;
            condition.bandwidth_kbps = bandwidth_kbps;
            condition.latency_ms = latency_ms;
            condition.packet_loss_rate = packet_loss_rate;
            condition.jitter_ms = jitter_ms;
            condition.congestion_level = packet_loss_rate * 2.0; // Simple congestion estimate
            condition.is_stable = (packet_loss_rate < 0.01 && jitter_ms < 50.0);
            condition.last_measurement = std::chrono::system_clock::now();

            g_adaptive_buffer_manager->update_network_condition(std::string(stream_id), condition);

            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_DEBUG,
                              "mod_audio_stream: Updated network conditions for stream %s "
                              "(bw=%.1fkbps, lat=%.1fms, loss=%.3f, jitter=%.1fms)\n",
                              stream_id,
                              bandwidth_kbps,
                              latency_ms,
                              packet_loss_rate,
                              jitter_ms);
        }
        catch (const std::exception &e)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream: Exception updating network conditions: %s\n",
                              e.what());
        }
    }

    /**
     * @brief Get buffer statistics
     */
    switch_status_t adaptive_buffer_get_stats(const char *stream_id, adaptive_buffer_stats_t *stats)
    {
        if (!g_adaptive_buffer_manager || !stream_id || !stats)
        {
            return SWITCH_STATUS_FALSE;
        }

        try
        {
            auto buffer_stats = g_adaptive_buffer_manager->get_buffer_statistics(std::string(stream_id));

            stats->total_messages = buffer_stats.total_messages;
            stats->dropped_messages = buffer_stats.dropped_messages;
            stats->current_message_count = buffer_stats.current_message_count;
            stats->current_size_bytes = buffer_stats.current_size_bytes;
            stats->max_size_reached = buffer_stats.max_size_reached;
            stats->underrun_events = buffer_stats.underrun_events;
            stats->overrun_events = buffer_stats.overrun_events;
            stats->adaptation_events = buffer_stats.adaptation_events;
            stats->average_latency_ms = buffer_stats.average_latency_ms;
            stats->current_latency_ms = buffer_stats.current_latency_ms;
            stats->packet_loss_rate = buffer_stats.packet_loss_rate;

            double utilization = g_adaptive_buffer_manager->get_buffer_utilization(std::string(stream_id));
            stats->buffer_utilization = utilization;

            size_t recommended_size = g_adaptive_buffer_manager->get_recommended_buffer_size(std::string(stream_id));
            stats->recommended_size_bytes = recommended_size;

            return SWITCH_STATUS_SUCCESS;
        }
        catch (const std::exception &e)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream: Exception getting buffer stats: %s\n",
                              e.what());
            return SWITCH_STATUS_FALSE;
        }
    }

} // extern "C"