// SPDX-License-Identifier: MIT
/**
 * @file connection_manager.hpp
 * @brief Connection resilience and reliability manager for mod_audio_stream
 *
 * This header provides comprehensive connection management with automatic
 * reconnection, failover support, health monitoring, and network adaptation.
 *
 * @author FreeSWITCH Community
 * @version 2.0
 * @date 2024
 */
#ifndef __CONNECTION_MANAGER_HPP__
#define __CONNECTION_MANAGER_HPP__

#include "mod_audio_stream.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/** @defgroup ConnectionManagement Connection Management and Resilience
 * @{
 */

/**
 * @brief Connection state enumeration
 */
typedef enum connection_state
{
    CONNECTION_DISCONNECTED = 0,
    CONNECTION_CONNECTING,
    CONNECTION_CONNECTED,
    CONNECTION_AUTHENTICATING,
    CONNECTION_READY,
    CONNECTION_RECONNECTING,
    CONNECTION_FAILED,
    CONNECTION_DEGRADED,
    CONNECTION_CLOSING
} connection_state_t;

/**
 * @brief Connection failure reasons
 */
typedef enum connection_failure_reason
{
    FAILURE_NONE = 0,
    FAILURE_NETWORK_TIMEOUT,
    FAILURE_DNS_RESOLUTION,
    FAILURE_SSL_HANDSHAKE,
    FAILURE_AUTHENTICATION,
    FAILURE_PROTOCOL_ERROR,
    FAILURE_SERVER_REJECTED,
    FAILURE_RATE_LIMITED,
    FAILURE_CERTIFICATE_ERROR,
    FAILURE_UNKNOWN
} connection_failure_reason_t;

/**
 * @brief Server endpoint configuration
 */
typedef struct server_endpoint
{
    std::string url;
    std::string hostname;
    uint16_t port;
    bool use_ssl;
    int priority; // 1 = highest priority
    bool is_healthy;
    uint32_t failure_count;
    std::chrono::system_clock::time_point last_attempt;
    std::chrono::system_clock::time_point last_success;
    double average_latency_ms;
    double success_rate; // 0.0 to 1.0
    std::string last_error;
} server_endpoint_t;

/**
 * @brief Reconnection policy configuration
 */
typedef struct reconnection_policy
{
    bool enable_reconnection;
    uint32_t max_reconnect_attempts;
    uint32_t initial_delay_ms;
    uint32_t max_delay_ms;
    double backoff_multiplier; // Exponential backoff factor
    uint32_t connection_timeout_ms;
    uint32_t health_check_interval_ms;
    bool enable_circuit_breaker;
    uint32_t circuit_breaker_threshold;
    uint32_t circuit_breaker_timeout_ms;
} reconnection_policy_t;

/**
 * @brief Network quality metrics
 */
typedef struct network_quality
{
    double latency_ms;
    double jitter_ms;
    double packet_loss_rate;
    double bandwidth_kbps;
    double signal_strength; // 0.0 to 1.0
    std::chrono::system_clock::time_point last_measurement;
} network_quality_t;

/**
 * @brief Connection statistics
 */
typedef struct connection_statistics
{
    uint64_t total_connections;
    uint64_t successful_connections;
    uint64_t failed_connections;
    uint64_t reconnection_attempts;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t messages_sent;
    uint64_t messages_received;
    std::chrono::system_clock::time_point connection_start_time;
    std::chrono::system_clock::time_point last_activity_time;
    double uptime_percentage;
} connection_statistics_t;

/**
 * @brief Circuit breaker state
 */
typedef enum circuit_breaker_state
{
    CIRCUIT_CLOSED = 0, // Normal operation
    CIRCUIT_OPEN,       // Failing, blocking requests
    CIRCUIT_HALF_OPEN   // Testing if service is back
} circuit_breaker_state_t;

/**
 * @brief Connection event types for callbacks
 */
typedef enum connection_event_type
{
    EVENT_CONNECTING = 0,
    EVENT_CONNECTED,
    EVENT_DISCONNECTED,
    EVENT_RECONNECTING,
    EVENT_FAILED,
    EVENT_HEALTH_CHECK,
    EVENT_QUALITY_CHANGE,
    EVENT_FAILOVER
} connection_event_type_t;

/**
 * @brief Connection event data
 */
typedef struct connection_event
{
    connection_event_type_t type;
    std::string stream_id;
    connection_state_t old_state;
    connection_state_t new_state;
    connection_failure_reason_t failure_reason;
    std::string server_url;
    network_quality_t quality;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
} connection_event_t;

/**
 * @brief Connection manager class
 */
class ConnectionManager
{
  public:
    /**
     * @brief Connection event callback function type
     */
    using ConnectionEventCallback = std::function<void(const connection_event_t &)>;

    /**
     * @brief Health check callback function type
     */
    using HealthCheckCallback = std::function<bool(const std::string &stream_id)>;

    /**
     * @brief Constructor
     */
    ConnectionManager();

    /**
     * @brief Destructor
     */
    ~ConnectionManager();

    /**
     * @brief Initialize connection manager
     */
    bool initialize(const reconnection_policy_t &policy);

    /**
     * @brief Cleanup connection manager
     */
    void cleanup();

    /**
     * @brief Create new connection with failover servers
     */
    bool create_connection(const std::string &stream_id,
                           const std::vector<server_endpoint_t> &servers,
                           const std::unordered_map<std::string, std::string> &connection_params);

    /**
     * @brief Close connection
     */
    bool close_connection(const std::string &stream_id, bool graceful = true);

    /**
     * @brief Get connection state
     */
    connection_state_t get_connection_state(const std::string &stream_id) const;

    /**
     * @brief Force reconnection
     */
    bool force_reconnect(const std::string &stream_id);

    /**
     * @brief Set connection event callback
     */
    void set_event_callback(ConnectionEventCallback callback)
    {
        event_callback_ = callback;
    }

    /**
     * @brief Set health check callback
     */
    void set_health_check_callback(HealthCheckCallback callback)
    {
        health_check_callback_ = callback;
    }

    /**
     * @brief Get connection statistics
     */
    connection_statistics_t get_connection_statistics(const std::string &stream_id) const;

    /**
     * @brief Get network quality metrics
     */
    network_quality_t get_network_quality(const std::string &stream_id) const;

    /**
     * @brief Update network quality metrics
     */
    void update_network_quality(const std::string &stream_id, const network_quality_t &quality);

    /**
     * @brief Get current server for connection
     */
    server_endpoint_t get_current_server(const std::string &stream_id) const;

    /**
     * @brief Get all servers for connection
     */
    std::vector<server_endpoint_t> get_servers(const std::string &stream_id) const;

    /**
     * @brief Add server to connection
     */
    bool add_server(const std::string &stream_id, const server_endpoint_t &server);

    /**
     * @brief Remove server from connection
     */
    bool remove_server(const std::string &stream_id, const std::string &server_url);

    /**
     * @brief Update server health status
     */
    void update_server_health(const std::string &stream_id, const std::string &server_url, bool is_healthy);

    /**
     * @brief Get overall system health
     */
    struct SystemHealth
    {
        uint32_t total_connections;
        uint32_t healthy_connections;
        uint32_t degraded_connections;
        uint32_t failed_connections;
        double average_latency_ms;
        double average_success_rate;
        std::chrono::system_clock::time_point last_check;
    };

    SystemHealth get_system_health() const;

    /**
     * @brief Enable/disable adaptive quality
     */
    void set_adaptive_quality_enabled(const std::string &stream_id, bool enabled);

    /**
     * @brief Get adaptive quality status
     */
    bool is_adaptive_quality_enabled(const std::string &stream_id) const;

    /**
     * @brief Manual failover to specific server
     */
    bool failover_to_server(const std::string &stream_id, const std::string &server_url);

    /**
     * @brief Get reconnection policy
     */
    const reconnection_policy_t &get_reconnection_policy() const
    {
        return policy_;
    }

    /**
     * @brief Update reconnection policy
     */
    bool update_reconnection_policy(const reconnection_policy_t &policy);

    /**
     * @brief Start connection monitoring
     */
    bool start_monitoring();

    /**
     * @brief Stop connection monitoring
     */
    void stop_monitoring();

    /**
     * @brief Get monitoring status
     */
    bool is_monitoring_active() const
    {
        return monitoring_active_;
    }

  private:
    /**
     * @brief Connection context for internal management
     */
    struct ConnectionContext
    {
        std::string stream_id;
        std::vector<server_endpoint_t> servers;
        std::unordered_map<std::string, std::string> connection_params;
        connection_state_t current_state;
        connection_statistics_t statistics;
        network_quality_t quality;

        // Reconnection state
        uint32_t reconnect_attempts;
        std::chrono::system_clock::time_point next_reconnect_time;
        connection_failure_reason_t last_failure_reason;

        // Circuit breaker
        circuit_breaker_state_t circuit_state;
        uint32_t consecutive_failures;
        std::chrono::system_clock::time_point circuit_open_time;

        // Current server
        size_t current_server_index;
        bool adaptive_quality_enabled;

        // Threading
        std::atomic<bool> should_reconnect;
        std::thread reconnect_thread;
        std::mutex context_mutex;
    };

    // Configuration
    reconnection_policy_t policy_;

    // Connection storage
    std::unordered_map<std::string, std::unique_ptr<ConnectionContext>> connections_;
    mutable std::mutex connections_mutex_;

    // Monitoring
    std::atomic<bool> monitoring_active_;
    std::thread monitoring_thread_;
    std::thread health_check_thread_;

    // Callbacks
    ConnectionEventCallback event_callback_;
    HealthCheckCallback health_check_callback_;

    // Internal methods
    void monitoring_worker();
    void health_check_worker();
    void reconnection_worker(const std::string &stream_id);

    bool attempt_connection(ConnectionContext &context, size_t server_index);
    bool select_next_server(ConnectionContext &context);
    void
    update_server_statistics(ConnectionContext &context, size_t server_index, bool success, double latency_ms = 0.0);

    void fire_connection_event(const connection_event_t &event);

    bool is_circuit_breaker_open(const ConnectionContext &context) const;
    void update_circuit_breaker(ConnectionContext &context, bool success);

    double calculate_exponential_backoff(uint32_t attempt) const;
    bool should_attempt_reconnection(const ConnectionContext &context) const;

    void sort_servers_by_priority(std::vector<server_endpoint_t> &servers);
    size_t find_best_server(const std::vector<server_endpoint_t> &servers);

    void update_connection_statistics(ConnectionContext &context, bool increment_messages = false);
    void measure_network_quality(ConnectionContext &context);

    bool validate_server_endpoint(const server_endpoint_t &server) const;
    std::string format_connection_error(connection_failure_reason_t reason) const;
};

/**
 * @brief Connection pool manager for efficient resource usage
 */
class ConnectionPool
{
  public:
    ConnectionPool(size_t max_connections = 100);
    ~ConnectionPool();

    /**
     * @brief Get connection from pool or create new
     */
    std::shared_ptr<ConnectionManager> get_connection_manager();

    /**
     * @brief Return connection to pool
     */
    void return_connection_manager(std::shared_ptr<ConnectionManager> manager);

    /**
     * @brief Get pool statistics
     */
    struct PoolStats
    {
        size_t total_connections;
        size_t active_connections;
        size_t idle_connections;
        size_t max_connections;
        uint64_t total_requests;
        uint64_t cache_hits;
        uint64_t cache_misses;
    };

    PoolStats get_pool_statistics() const;

    /**
     * @brief Clear all idle connections
     */
    void clear_idle_connections();

  private:
    std::queue<std::shared_ptr<ConnectionManager>> idle_connections_;
    std::unordered_map<std::string, std::shared_ptr<ConnectionManager>> active_connections_;
    size_t max_connections_;
    mutable std::mutex pool_mutex_;

    PoolStats stats_;
};

/**
 * @brief Default reconnection policies
 */
namespace ReconnectionPolicies
{
/**
 * @brief Conservative policy for stable networks
 */
static const reconnection_policy_t Conservative = {.enable_reconnection = true,
                                                   .max_reconnect_attempts = 5,
                                                   .initial_delay_ms = 1000,
                                                   .max_delay_ms = 30000,
                                                   .backoff_multiplier = 2.0,
                                                   .connection_timeout_ms = 10000,
                                                   .health_check_interval_ms = 30000,
                                                   .enable_circuit_breaker = true,
                                                   .circuit_breaker_threshold = 5,
                                                   .circuit_breaker_timeout_ms = 60000};

/**
 * @brief Aggressive policy for real-time applications
 */
static const reconnection_policy_t Aggressive = {.enable_reconnection = true,
                                                 .max_reconnect_attempts = 10,
                                                 .initial_delay_ms = 500,
                                                 .max_delay_ms = 15000,
                                                 .backoff_multiplier = 1.5,
                                                 .connection_timeout_ms = 5000,
                                                 .health_check_interval_ms = 15000,
                                                 .enable_circuit_breaker = true,
                                                 .circuit_breaker_threshold = 3,
                                                 .circuit_breaker_timeout_ms = 30000};

/**
 * @brief Balanced policy for general use
 */
static const reconnection_policy_t Balanced = {.enable_reconnection = true,
                                               .max_reconnect_attempts = 7,
                                               .initial_delay_ms = 750,
                                               .max_delay_ms = 20000,
                                               .backoff_multiplier = 1.8,
                                               .connection_timeout_ms = 8000,
                                               .health_check_interval_ms = 20000,
                                               .enable_circuit_breaker = true,
                                               .circuit_breaker_threshold = 4,
                                               .circuit_breaker_timeout_ms = 45000};
} // namespace ReconnectionPolicies

/** @} */ // End of ConnectionManagement group

#endif /* __CONNECTION_MANAGER_HPP__ */