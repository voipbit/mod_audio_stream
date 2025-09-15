// SPDX-License-Identifier: MIT
/**
 * @file stream_utils.hpp
 * @brief Utility classes and functions for audio streaming
 *
 * This header defines utility classes and functions used throughout the
 * audio streaming module, including ring buffers for audio data management,
 * codec definitions, and JSON message generation utilities.
 *
 * @author FreeSWITCH Community
 * @version 1.0
 * @date 2024
 */
#ifndef __STREAM_UTILS_HPP__
#define __STREAM_UTILS_HPP__

#include <mutex>
#include <string>
#include <switch.h>
#include <switch_json.h>

/** @defgroup AudioConstants Audio Processing Constants
 * @{
 */

/** @brief L16 PCM frame size for 20ms at 8kHz sampling rate, mono channel */
#define L16_FRAME_SIZE_8KHZ_20MS 320

/** @brief μ-law frame size for 20ms at 8kHz sampling rate, mono channel */
#define ULAW_FRAME_SIZE_8KHZ_20MS 160

/** @brief Maximum number of connection attempts before giving up */
#define MAX_CONNECTION_ATTEMPTS 3

/** @brief Delay in seconds between reconnection attempts */
#define RECONNECTION_DELAY_SECONDS 1

/** @} */ // End of AudioConstants group

/** @defgroup DataTypes Data Types and Enumerations
 * @{
 */

/**
 * @brief Audio codec enumeration for streaming
 *
 * Defines the supported audio codecs for WebSocket streaming.
 * Each codec has different bandwidth and quality characteristics.
 */
typedef enum streaming_codec
{
    /** @brief Linear 16-bit PCM codec (highest quality, highest bandwidth) */
    L16,

    /** @brief μ-law codec (8-bit compressed, lower bandwidth) */
    ULAW
} streaming_codec_t;

/**
 * @brief Thread-safe ring buffer for audio data streaming
 *
 * This class provides a thread-safe ring buffer implementation for managing
 * audio data chunks during streaming operations. It includes timing information
 * for proper audio synchronization and flow control.
 */
class Buffer
{
    // Prevent copying and assignment
    Buffer(const Buffer &) = delete;
    void operator=(const Buffer &) = delete;

  private:
    /** @brief Recursive mutex for thread-safe buffer access */
    std::recursive_mutex mutex_;

    /** @brief Underlying FreeSWITCH buffer for data storage */
    switch_buffer_t *freeswitch_buffer_;

    /** @brief Time increment per audio chunk (typically 20ms) */
    switch_time_t time_step_increment_;

    /** @brief Timestamp when buffering started */
    switch_time_t start_time_;

    /** @brief Timestamp when buffering ended */
    switch_time_t end_time_;

    /** @brief Generated time (updated regardless of packet drops) */
    switch_time_t generated_time_;

    /** @brief Generated chunk counter */
    uint32_t generated_chunk_count_;

  public:
    /** @brief Size of each audio chunk in bytes (typically 20ms worth of data) */
    uint32_t chunk_size_bytes_;

    /** @brief Current number of bytes used in buffer */
    uint32_t current_usage_bytes_;

    /** @brief Maximum buffer capacity in bytes */
    uint32_t maximum_capacity_bytes_;

    /** @brief Flag indicating if degradation notification has been sent */
    uint8_t degradation_notification_sent_;

    /** @brief Stream identifier for this buffer */
    std::string stream_identifier_;

    /** @brief Timestamp of last data transmission */
    switch_time_t last_send_time_;

    /** @brief Counter for transmitted chunks */
    uint32_t transmitted_chunk_count_;
    /**
     * @brief Constructor for audio buffer
     *
     * @param stream_id Unique identifier for the stream
     * @param max_capacity_bytes Maximum buffer capacity in bytes
     * @param chunk_size_bytes Size of each audio chunk in bytes
     * @param time_increment_microseconds Time increment per chunk in microseconds
     */
    Buffer(std::string &stream_id, size_t max_capacity_bytes, int chunk_size_bytes, int time_increment_microseconds);

    /**
     * @brief Destructor - cleans up buffer resources
     */
    ~Buffer();

    /**
     * @brief Attempt to acquire buffer lock without blocking
     * @return true if lock was acquired, false otherwise
     */
    bool try_lock()
    {
        return mutex_.try_lock();
    }

    /**
     * @brief Acquire buffer lock (blocking)
     * @return true when lock is acquired
     */
    bool lock()
    {
        mutex_.lock();
        return true;
    }

    /**
     * @brief Release buffer lock
     */
    void unlock()
    {
        mutex_.unlock();
    }

    /**
     * @brief Write audio data to buffer
     * @param data Pointer to audio data to write
     * @return true if data was written successfully, false if buffer is full
     */
    bool write(void *data);

    /**
     * @brief Read audio data from buffer
     * @param data Pointer to receive data pointer
     * @param data_length Pointer to receive data length
     * @return true if data was read successfully, false if buffer is empty
     */
    bool read(void **data, size_t *data_length);

    /**
     * @brief Check if buffer contains data
     * @return true if data is available, false if buffer is empty
     */
    bool is_data_available()
    {
        return (current_usage_bytes_ > 0);
    }

    /**
     * @brief Set the start time for buffer operations
     * @param time Start time in FreeSWITCH time format
     */
    void set_start_time(switch_time_t time)
    {
        start_time_ = time;
    }
};

/**
 * @brief Client event types for JSON message generation
 *
 * Defines the types of events that can be sent to the remote WebSocket server
 * as part of the streaming protocol.
 */
typedef enum client_event_type
{
    /** @brief Stream start event - initial handshake */
    CLIENT_EVENT_START,

    /** @brief Media data event - contains audio payload */
    CLIENT_EVENT_MEDIA,

    /** @brief Stream stop event - graceful termination */
    CLIENT_EVENT_STOP
} client_event_type_t;

/** @} */ // End of DataTypes group

/** @defgroup UtilityFunctions Utility Functions
 * @{
 */

/**
 * @brief Generate JSON message for WebSocket transmission
 *
 * Creates a JSON-formatted message containing audio data and metadata
 * for transmission to the remote WebSocket server. The message format
 * follows the streaming protocol specification.
 *
 * @param event_type Type of event (start, media, stop)
 * @param sequence_number Sequential message number for ordering
 * @param session_uuid FreeSWITCH session UUID
 * @param stream_identifier Unique stream identifier
 * @param track_type Audio track type ("inbound", "outbound", "both")
 * @param audio_buffer Buffer containing audio data (NULL for non-media events)
 * @param extra_headers Additional HTTP headers as JSON string
 * @param codec Audio codec being used (L16 or ULAW)
 * @param sampling_rate Audio sampling rate in Hz
 * @return Allocated JSON string (caller must free) or NULL on error
 */
char *generate_json_data_event(client_event_type_t event_type,
                               int sequence_number,
                               std::string &session_uuid,
                               std::string &stream_identifier,
                               std::string &track_type,
                               Buffer *audio_buffer,
                               std::string &extra_headers,
                               streaming_codec_t codec,
                               int sampling_rate);

/** @} */ // End of UtilityFunctions group

#endif /* __STREAM_UTILS_HPP__ */
