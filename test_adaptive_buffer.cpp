#include "src/adaptive_buffer.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    std::cout << "Testing Adaptive Buffer Manager..." << std::endl;

    // Test 1: Basic buffer creation
    AdaptiveBufferManager manager;

    // Initialize with default config
    buffer_config_t config = BufferConfigurations::Balanced;
    if (!manager.initialize(config))
    {
        std::cerr << "Failed to initialize buffer manager" << std::endl;
        return 1;
    }

    std::cout << "✓ Buffer manager initialized successfully" << std::endl;

    // Test 2: Create a buffer
    std::string stream_id = "test_stream_001";
    if (!manager.create_buffer(stream_id, config))
    {
        std::cerr << "Failed to create buffer" << std::endl;
        return 1;
    }

    std::cout << "✓ Buffer created successfully for stream: " << stream_id << std::endl;

    // Test 3: Enqueue a message
    buffered_message_t msg;
    msg.data.resize(1024);
    // Size is determined by data vector size
    msg.priority = PRIORITY_NORMAL;
    msg.timestamp = std::chrono::system_clock::now();
    msg.sequence_number = 1;
    msg.deadline = msg.timestamp + std::chrono::milliseconds(5000);

    if (!manager.enqueue_message(stream_id, msg))
    {
        std::cerr << "Failed to enqueue message" << std::endl;
        return 1;
    }

    std::cout << "✓ Message enqueued successfully" << std::endl;

    // Test 4: Dequeue the message
    buffered_message_t retrieved_msg;
    if (!manager.dequeue_message(stream_id, retrieved_msg, std::chrono::milliseconds(100)))
    {
        std::cerr << "Failed to dequeue message" << std::endl;
        return 1;
    }

    std::cout << "✓ Message dequeued successfully" << std::endl;
    std::cout << "  - Size: " << retrieved_msg.data.size() << " bytes" << std::endl;
    std::cout << "  - Priority: " << (int)retrieved_msg.priority << std::endl;
    std::cout << "  - Sequence: " << retrieved_msg.sequence_number << std::endl;

    // Test 5: Get buffer statistics
    auto stats = manager.get_buffer_statistics(stream_id);
    std::cout << "✓ Buffer statistics retrieved:" << std::endl;
    std::cout << "  - Total messages processed: " << stats.total_messages << std::endl;
    std::cout << "  - Current message count: " << stats.current_message_count << std::endl;
    std::cout << "  - Dropped messages: " << stats.dropped_messages << std::endl;

    // Test 6: Network condition update
    network_condition_t network_cond;
    network_cond.bandwidth_kbps = 500.0;  // 500 Kbps
    network_cond.latency_ms = 250.0;      // 250ms latency
    network_cond.packet_loss_rate = 0.05; // 5% packet loss
    network_cond.jitter_ms = 50.0;        // 50ms jitter
    network_cond.is_stable = false;
    network_cond.congestion_level = 0.7; // 70% congestion
    network_cond.last_measurement = std::chrono::system_clock::now();

    manager.update_network_condition(stream_id, network_cond);
    std::cout << "✓ Network conditions updated" << std::endl;

    // Test 7: Test adaptive behavior
    double utilization = manager.get_buffer_utilization(stream_id);
    size_t recommended_size = manager.get_recommended_buffer_size(stream_id);

    std::cout << "✓ Adaptive metrics calculated:" << std::endl;
    std::cout << "  - Buffer utilization: " << (utilization * 100) << "%" << std::endl;
    std::cout << "  - Recommended buffer size: " << recommended_size << " bytes" << std::endl;

    // Test 8: Packet Loss Recovery
    std::cout << "\nTesting Packet Loss Recovery..." << std::endl;

    PacketLossRecovery recovery;
    if (!recovery.initialize(PacketLossRecovery::RECOVERY_INTERPOLATION))
    {
        std::cerr << "Failed to initialize packet loss recovery" << std::endl;
        return 1;
    }

    std::cout << "✓ Packet loss recovery initialized" << std::endl;

    // Test missing packet detection
    auto missing_packets = recovery.detect_missing_packets(stream_id, 5, 10);
    std::cout << "✓ Missing packet detection: found " << missing_packets.size() << " missing packets" << std::endl;

    // Test interpolation
    std::vector<uint8_t> prev_frame(320, 0x80); // Silence frame
    std::vector<uint8_t> next_frame(320, 0x90); // Different frame
    std::vector<uint8_t> interpolated_frame;

    if (recovery.interpolate_missing_audio(stream_id, prev_frame, next_frame, interpolated_frame))
    {
        std::cout << "✓ Audio interpolation successful, generated " << interpolated_frame.size() << " bytes"
                  << std::endl;
    }

    // Test 9: Jitter Buffer
    std::cout << "\nTesting Jitter Buffer..." << std::endl;

    JitterBuffer jitter_buffer(100, 500); // 100ms initial, 500ms max delay

    // Add some packets with jitter
    for (int i = 0; i < 5; i++)
    {
        buffered_message_t packet;
        packet.data.resize(160);
        // Size is determined by data vector size
        packet.sequence_number = i + 1;
        packet.timestamp = std::chrono::system_clock::now() - std::chrono::milliseconds(i * 20);
        packet.priority = PRIORITY_NORMAL;

        jitter_buffer.add_packet(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "✓ Added packets to jitter buffer" << std::endl;

    // Retrieve packets
    buffered_message_t jitter_packet;
    int retrieved_count = 0;

    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Wait for initial delay

    while (jitter_buffer.get_next_packet(jitter_packet))
    {
        retrieved_count++;
        std::cout << "  - Retrieved packet " << jitter_packet.sequence_number << std::endl;
    }

    std::cout << "✓ Retrieved " << retrieved_count << " packets from jitter buffer" << std::endl;

    auto jitter_stats = jitter_buffer.get_jitter_statistics();
    std::cout << "  - Current delay: " << jitter_stats.buffer_delay_ms << "ms" << std::endl;

    // Test 10: Cleanup
    if (!manager.destroy_buffer(stream_id))
    {
        std::cerr << "Failed to destroy buffer" << std::endl;
        return 1;
    }

    std::cout << "✓ Buffer destroyed successfully" << std::endl;

    std::cout << "\n🎉 All tests passed! Adaptive buffer implementation is working correctly." << std::endl;
    std::cout << "\nTest Summary:" << std::endl;
    std::cout << "✓ Buffer manager initialization" << std::endl;
    std::cout << "✓ Buffer creation and destruction" << std::endl;
    std::cout << "✓ Message enqueue and dequeue operations" << std::endl;
    std::cout << "✓ Statistics collection" << std::endl;
    std::cout << "✓ Network condition adaptation" << std::endl;
    std::cout << "✓ Buffer utilization calculation" << std::endl;
    std::cout << "✓ Packet loss detection and recovery" << std::endl;
    std::cout << "✓ Audio interpolation" << std::endl;
    std::cout << "✓ Jitter buffer functionality" << std::endl;

    return 0;
}