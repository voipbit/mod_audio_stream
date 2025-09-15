// SPDX-License-Identifier: MIT
/**
 * @file adaptive_buffer.hpp
 * @brief Advanced adaptive buffering and flow control for mod_audio_stream
 *
 * This header provides intelligent buffering with network adaptation,
 * priority queuing, packet loss recovery, and dynamic flow control.
 *
 * @author FreeSWITCH Community
 * @version 2.0
 * @date 2024
 */
#ifndef __ADAPTIVE_BUFFER_HPP__
#define __ADAPTIVE_BUFFER_HPP__

#include "connection_manager.hpp"
#include "mod_audio_stream.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

/** @defgroup AdaptiveBuffering Adaptive Buffering and Flow Control
 * @{
 */

/**
 * @brief Buffer state enumeration
 */
typedef enum buffer_state
{
    BUFFER_NORMAL = 0,
    BUFFER_UNDERRUN,
    BUFFER_OVERRUN,
    BUFFER_ADAPTING,
    BUFFER_RECOVERING,
    BUFFER_DRAINING
} buffer_state_t;

/**
 * @brief Message priority levels
 */
typedef enum message_priority
{
    PRIORITY_CRITICAL = 0, // Control messages, must be delivered
    PRIORITY_HIGH,         // Real-time audio data
    PRIORITY_NORMAL,       // Standard audio data
    PRIORITY_LOW,          // Background data, statistics
    PRIORITY_BULK          // File transfers, non-time-sensitive
} message_priority_t;

/**
 * @brief Flow control strategy
 */
typedef enum flow_control_strategy
{
    FLOW_CONTROL_NONE = 0,
    FLOW_CONTROL_STOP_AND_WAIT,
    FLOW_CONTROL_SLIDING_WINDOW,
    FLOW_CONTROL_TOKEN_BUCKET,
    FLOW_CONTROL_ADAPTIVE_RATE
} flow_control_strategy_t;

/**
 * @brief Buffer configuration
 */
typedef struct buffer_config
{
    size_t initial_size_bytes;
    size_t min_size_bytes;
    size_t max_size_bytes;
    size_t target_latency_ms;
    size_t max_latency_ms;

    // Adaptation parameters
    double growth_factor; // How fast to grow buffer
    double shrink_factor; // How fast to shrink buffer
    uint32_t adaptation_interval_ms;
    uint32_t stability_threshold_ms;

    // Quality thresholds
    double max_packet_loss_rate;
    double max_jitter_ms;
    uint32_t underrun_threshold;
    uint32_t overrun_threshold;

    // Flow control
    flow_control_strategy_t flow_control;
    size_t window_size;
    double token_bucket_rate;
    size_t token_bucket_capacity;

} buffer_config_t;

/**
 * @brief Buffered message structure
 */
typedef struct buffered_message
{
    uint32_t sequence_number;
    message_priority_t priority;
    std::vector<uint8_t> data;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point deadline;
    uint32_t retry_count;
    std::string stream_id;
    std::unordered_map<std::string, std::string> metadata;
} buffered_message_t;

/**
 * @brief Buffer statistics
 */
typedef struct buffer_statistics
{
    size_t current_size_bytes;
    size_t current_message_count;
    size_t max_size_reached;
    double current_latency_ms;
    double average_latency_ms;
    double jitter_ms;

    uint64_t total_messages;
    uint64_t dropped_messages;
    uint64_t duplicate_messages;
    uint64_t out_of_order_messages;
    uint64_t expired_messages;

    uint32_t underrun_events;
    uint32_t overrun_events;
    uint32_t adaptation_events;

    double throughput_bps;
    double packet_loss_rate;
    std::chrono::system_clock::time_point last_update;
} buffer_statistics_t;

/**
 * @brief Network condition assessment
 */
typedef struct network_condition
{
    double bandwidth_kbps;
    double latency_ms;
    double jitter_ms;
    double packet_loss_rate;
    double congestion_level; // 0.0 to 1.0
    bool is_stable;
    std::chrono::system_clock::time_point last_measurement;
} network_condition_t;

/**
 * @brief Adaptive buffer manager
 */
class AdaptiveBufferManager
{
  public:
    /**
     * @brief Buffer event callback function type
     */
    using BufferEventCallback =
        std::function<void(const std::string &stream_id, buffer_state_t old_state, buffer_state_t new_state)>;

    /**
     * @brief Flow control callback function type
     */
    using FlowControlCallback = std::function<void(const std::string &stream_id, bool should_pause)>;

    /**
     * @brief Constructor
     */
    AdaptiveBufferManager();

    /**
     * @brief Destructor
     */
    ~AdaptiveBufferManager();

    /**
     * @brief Initialize buffer manager
     */
    bool initialize(const buffer_config_t &config);

    /**
     * @brief Create buffer for stream
     */
    bool create_buffer(const std::string &stream_id, const buffer_config_t &config);

    /**
     * @brief Destroy buffer for stream
     */
    bool destroy_buffer(const std::string &stream_id);

    /**
     * @brief Enqueue message with priority
     */
    bool enqueue_message(const std::string &stream_id, const buffered_message_t &message);

    /**
     * @brief Dequeue message (blocking)
     */
    bool dequeue_message(const std::string &stream_id,
                         buffered_message_t &message,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /**
     * @brief Peek at next message without removing
     */
    bool peek_message(const std::string &stream_id, buffered_message_t &message) const;

    /**
     * @brief Get buffer statistics
     */
    buffer_statistics_t get_buffer_statistics(const std::string &stream_id) const;

    /**
     * @brief Update network condition
     */
    void update_network_condition(const std::string &stream_id, const network_condition_t &condition);

    /**
     * @brief Get current buffer state
     */
    buffer_state_t get_buffer_state(const std::string &stream_id) const;

    /**
     * @brief Force buffer adaptation
     */
    bool adapt_buffer(const std::string &stream_id);

    /**
     * @brief Set buffer event callback
     */
    void set_buffer_event_callback(BufferEventCallback callback)
    {
        buffer_event_callback_ = callback;
    }

    /**
     * @brief Set flow control callback
     */
    void set_flow_control_callback(FlowControlCallback callback)
    {
        flow_control_callback_ = callback;
    }

    /**
     * @brief Enable/disable adaptive buffering
     */
    void set_adaptive_enabled(const std::string &stream_id, bool enabled);

    /**
     * @brief Check if adaptive buffering is enabled
     */
    bool is_adaptive_enabled(const std::string &stream_id) const;

    /**
     * @brief Flush buffer (emergency drain)
     */
    bool flush_buffer(const std::string &stream_id, message_priority_t min_priority = PRIORITY_LOW);

    /**
     * @brief Get buffer utilization (0.0 to 1.0)
     */
    double get_buffer_utilization(const std::string &stream_id) const;

    /**
     * @brief Get recommended buffer size based on network conditions
     */
    size_t get_recommended_buffer_size(const std::string &stream_id) const;

    /**
     * @brief Start buffer monitoring
     */
    bool start_monitoring();

    /**
     * @brief Stop buffer monitoring
     */
    void stop_monitoring();

    /**
     * @brief Check if monitoring is active
     */
    bool is_monitoring_active() const
    {
        return monitoring_active_;
    }

    /**
     * @brief Priority queue comparator for messages
     */
    struct MessageComparator
    {
        bool operator()(const buffered_message_t &a, const buffered_message_t &b) const
        {
            // Higher priority first, then by timestamp for same priority
            if (a.priority != b.priority)
            {
                return a.priority > b.priority; // Lower enum value = higher priority
            }
            return a.timestamp > b.timestamp; // Earlier timestamp first
        }
    };

  private:
    /**
     * @brief Buffer context for internal management
     */
    struct BufferContext
    {
        std::string stream_id;
        buffer_config_t config;
        buffer_state_t current_state;
        buffer_statistics_t statistics;
        network_condition_t network_condition;

        // Message storage
        std::priority_queue<buffered_message_t, std::vector<buffered_message_t>, MessageComparator> message_queue;

        // Flow control state
        size_t current_window_size;
        double token_bucket_tokens;
        std::chrono::system_clock::time_point last_token_update;

        // Adaptation state
        bool adaptive_enabled;
        std::chrono::system_clock::time_point last_adaptation;
        size_t stable_period_ms;

        // Sequence tracking
        uint32_t expected_sequence;
        std::unordered_map<uint32_t, buffered_message_t> out_of_order_messages;

        // Threading
        mutable std::mutex context_mutex;
        std::condition_variable data_available;
        std::atomic<bool> should_stop;
    };

    // Configuration
    buffer_config_t default_config_;

    // Buffer storage
    std::unordered_map<std::string, std::unique_ptr<BufferContext>> buffers_;
    mutable std::mutex buffers_mutex_;

    // Monitoring
    std::atomic<bool> monitoring_active_;
    std::thread monitoring_thread_;

    // Callbacks
    BufferEventCallback buffer_event_callback_;
    FlowControlCallback flow_control_callback_;

    // Internal methods
    void monitoring_worker();
    void adapt_buffer_size(BufferContext &context);
    void update_buffer_statistics(BufferContext &context);
    void check_buffer_conditions(BufferContext &context);
    void handle_out_of_order_messages(BufferContext &context);
    void apply_flow_control(BufferContext &context);

    bool should_drop_message(const BufferContext &context, const buffered_message_t &message) const;
    void expire_old_messages(BufferContext &context);
    void reorder_messages(BufferContext &context);

    void fire_buffer_event(const std::string &stream_id, buffer_state_t old_state, buffer_state_t new_state);

    // Flow control implementations
    bool token_bucket_allow(BufferContext &context, size_t message_size);
    void update_sliding_window(BufferContext &context);
    bool rate_limit_check(BufferContext &context, size_t message_size);

    // Network adaptation
    size_t calculate_optimal_buffer_size(const BufferContext &context) const;
    double estimate_required_bandwidth(const BufferContext &context) const;
    bool detect_congestion(const BufferContext &context) const;
    void adjust_for_packet_loss(BufferContext &context);
};

/**
 * @brief Packet loss recovery system
 */
class PacketLossRecovery
{
  public:
    /**
     * @brief Recovery strategy
     */
    typedef enum recovery_strategy
    {
        RECOVERY_NONE = 0,
        RECOVERY_RETRANSMIT,    // Request retransmission
        RECOVERY_FEC,           // Forward Error Correction
        RECOVERY_INTERPOLATION, // Audio interpolation
        RECOVERY_SILENCE        // Insert silence
    } recovery_strategy_t;

    PacketLossRecovery();
    ~PacketLossRecovery();

    /**
     * @brief Initialize recovery system
     */
    bool initialize(recovery_strategy_t strategy);

    /**
     * @brief Detect missing packets
     */
    std::vector<uint32_t>
    detect_missing_packets(const std::string &stream_id, uint32_t last_sequence, uint32_t current_sequence);

    /**
     * @brief Request packet retransmission
     */
    bool request_retransmission(const std::string &stream_id, const std::vector<uint32_t> &missing_sequences);

    /**
     * @brief Perform audio interpolation for missing data
     */
    bool interpolate_missing_audio(const std::string &stream_id,
                                   const std::vector<uint8_t> &previous_frame,
                                   const std::vector<uint8_t> &next_frame,
                                   std::vector<uint8_t> &interpolated_frame);

    /**
     * @brief Get recovery statistics
     */
    struct RecoveryStats
    {
        uint64_t packets_lost;
        uint64_t packets_recovered;
        uint64_t retransmissions_requested;
        uint64_t interpolations_performed;
        double recovery_rate;
    };

    RecoveryStats get_recovery_statistics(const std::string &stream_id) const;

  private:
    recovery_strategy_t strategy_;
    std::unordered_map<std::string, RecoveryStats> recovery_stats_;
    mutable std::mutex stats_mutex_;
};

/**
 * @brief Jitter buffer implementation
 */
class JitterBuffer
{
  public:
    JitterBuffer(size_t initial_size_ms = 60, size_t max_size_ms = 200);
    ~JitterBuffer();

    /**
     * @brief Add packet to jitter buffer
     */
    bool add_packet(const buffered_message_t &packet);

    /**
     * @brief Get next packet for playback
     */
    bool get_next_packet(buffered_message_t &packet);

    /**
     * @brief Adapt buffer size based on jitter
     */
    void adapt_to_jitter(double current_jitter_ms);

    /**
     * @brief Get current buffer delay
     */
    double get_current_delay_ms() const;

    /**
     * @brief Get jitter statistics
     */
    struct JitterStats
    {
        double current_jitter_ms;
        double max_jitter_ms;
        double buffer_delay_ms;
        uint64_t late_packets;
        uint64_t early_packets;
        uint64_t duplicate_packets;
    };

    JitterStats get_jitter_statistics() const;

  private:
    size_t target_delay_ms_;
    size_t max_delay_ms_;
    std::priority_queue<buffered_message_t, std::vector<buffered_message_t>, AdaptiveBufferManager::MessageComparator>
        jitter_queue_;
    JitterStats stats_;
    mutable std::mutex jitter_mutex_;

    void update_jitter_statistics(const buffered_message_t &packet);
    bool should_play_packet(const buffered_message_t &packet) const;
};

/**
 * @brief Pre-defined buffer configurations
 */
namespace BufferConfigurations
{
/**
 * @brief Low-latency configuration for real-time applications
 */
static const buffer_config_t LowLatency = {.initial_size_bytes = 8192, // 8KB
                                           .min_size_bytes = 4096,     // 4KB
                                           .max_size_bytes = 32768,    // 32KB
                                           .target_latency_ms = 50,    // 50ms target
                                           .max_latency_ms = 100,      // 100ms max
                                           .growth_factor = 1.2,
                                           .shrink_factor = 0.9,
                                           .adaptation_interval_ms = 100,
                                           .stability_threshold_ms = 1000,
                                           .max_packet_loss_rate = 0.02, // 2%
                                           .max_jitter_ms = 20.0,
                                           .underrun_threshold = 3,
                                           .overrun_threshold = 5,
                                           .flow_control = FLOW_CONTROL_TOKEN_BUCKET,
                                           .window_size = 10,
                                           .token_bucket_rate = 1000.0, // tokens/sec
                                           .token_bucket_capacity = 100};

/**
 * @brief High-quality configuration for recording/streaming
 */
static const buffer_config_t HighQuality = {.initial_size_bytes = 65536, // 64KB
                                            .min_size_bytes = 32768,     // 32KB
                                            .max_size_bytes = 262144,    // 256KB
                                            .target_latency_ms = 200,    // 200ms target
                                            .max_latency_ms = 500,       // 500ms max
                                            .growth_factor = 1.5,
                                            .shrink_factor = 0.8,
                                            .adaptation_interval_ms = 500,
                                            .stability_threshold_ms = 3000,
                                            .max_packet_loss_rate = 0.001, // 0.1%
                                            .max_jitter_ms = 50.0,
                                            .underrun_threshold = 2,
                                            .overrun_threshold = 3,
                                            .flow_control = FLOW_CONTROL_SLIDING_WINDOW,
                                            .window_size = 20,
                                            .token_bucket_rate = 2000.0,
                                            .token_bucket_capacity = 200};

/**
 * @brief Balanced configuration for general use
 */
static const buffer_config_t Balanced = {.initial_size_bytes = 32768, // 32KB
                                         .min_size_bytes = 16384,     // 16KB
                                         .max_size_bytes = 131072,    // 128KB
                                         .target_latency_ms = 120,    // 120ms target
                                         .max_latency_ms = 300,       // 300ms max
                                         .growth_factor = 1.3,
                                         .shrink_factor = 0.85,
                                         .adaptation_interval_ms = 250,
                                         .stability_threshold_ms = 2000,
                                         .max_packet_loss_rate = 0.01, // 1%
                                         .max_jitter_ms = 30.0,
                                         .underrun_threshold = 3,
                                         .overrun_threshold = 4,
                                         .flow_control = FLOW_CONTROL_ADAPTIVE_RATE,
                                         .window_size = 15,
                                         .token_bucket_rate = 1500.0,
                                         .token_bucket_capacity = 150};
} // namespace BufferConfigurations

/** @} */ // End of AdaptiveBuffering group

#endif /* __ADAPTIVE_BUFFER_HPP__ */