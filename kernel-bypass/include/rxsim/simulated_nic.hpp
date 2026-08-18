#pragma once

#include "rxsim/virtual_interface.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace rxsim {

// Hardware stand-in. A thread drains packets (synthetic or UDP) and either
// fake-DMAs them into a posted buffer or records an RX overrun.
//
// Interview honesty: the kernel UDP stack still runs on *this* thread.
// What we bypass is the application path. memcpy is fake DMA; a real NIC
// writes registered memory without the CPU.
class SimulatedNic {
public:
    explicit SimulatedNic(VirtualInterface& vi);
    ~SimulatedNic();

    SimulatedNic(const SimulatedNic&) = delete;
    SimulatedNic& operator=(const SimulatedNic&) = delete;

    // Produce `packet_count` synthetic packets on the calling thread.
    // If the descriptor ring is empty the packet is dropped (rx_overrun),
    // unless wait_for_buffer is set (used by the concurrent ownership test).
    void run_fake(std::uint64_t packet_count, std::uint16_t payload_size,
                  bool yield_each = false, bool wait_for_buffer = false);

    // Bind a UDP socket and drain it on a background thread with recvmmsg.
    // Linux/WSL only.
    void start_udp(std::uint16_t port, int batch = 32);

    void stop();
    void join();

    [[nodiscard]] bool running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    bool dma_or_drop(const void* src, std::uint16_t length, bool wait_for_buffer = false);
    void udp_loop();

    VirtualInterface& vi_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_{-1};
    int batch_{32};
};

}  // namespace rxsim
