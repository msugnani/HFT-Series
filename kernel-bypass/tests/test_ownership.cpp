#include "rxsim/packet.hpp"
#include "rxsim/simulated_nic.hpp"
#include "rxsim/virtual_interface.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

void test_post_poll_repost() {
    rxsim::VirtualInterface vi(8, 256);
    require(vi.post_all() == 8, "posted every Free buffer");
    require(!vi.post(0), "double-post must fail");

    rxsim::SimulatedNic nic(vi);
    nic.run_fake(3, 16);

    rxsim::RxEvent events[8]{};
    const int n = vi.poll(events, 8);
    require(n == 3, "three completions");
    require(vi.stats().completed.load() == 3, "completed=3");
    require(vi.stats().rx_overrun.load() == 0, "no overrun");

    for (int i = 0; i < n; ++i) {
        require(events[i].type == rxsim::RxEvent::Type::RxComplete, "rx complete");
        require(vi.state(events[i].buffer_id) == rxsim::BufferState::Filled, "Filled after complete");
        const auto* p = vi.data(events[i].buffer_id);
        require(p != nullptr, "data readable while Filled");
        std::uint64_t seq = 0;
        std::memcpy(&seq, p, sizeof(seq));
        require(seq == static_cast<std::uint64_t>(i), "fake producer wrote the sequence");
        require(vi.repost(events[i].buffer_id), "repost Filled buffer");
        require(vi.state(events[i].buffer_id) == rxsim::BufferState::Posted, "Posted after repost");
    }
}

void test_overflow_without_consumer() {
    // Four posted buffers, no one polling/reposting. Ten packets → 4 DMA, 6 drops.
    rxsim::VirtualInterface vi(4, 256);
    require(vi.post_all() == 4, "post 4");

    rxsim::SimulatedNic nic(vi);
    nic.run_fake(10, 16);

    require(vi.stats().completed.load() == 4, "ring absorbed 4");
    require(vi.stats().rx_overrun.load() == 6, "remaining 6 overran");
    require(vi.stats().dropped.load() == 6, "dropped tracks overrun");
}

void test_larger_ring_absorbs_burst_then_sustained_drop() {
    // A ring of 64 absorbs a burst of 64; further packets drop until repost.
    rxsim::VirtualInterface vi(64, 256);
    vi.post_all();

    rxsim::SimulatedNic nic(vi);
    nic.run_fake(64, 16);
    require(vi.stats().completed.load() == 64, "burst of 64 fits");
    require(vi.stats().rx_overrun.load() == 0, "burst did not drop");

    nic.run_fake(10, 16);
    require(vi.stats().rx_overrun.load() == 10, "sustained extra packets drop");
}

void test_concurrent_consumer_keeps_up() {
    constexpr int kPackets = 50000;
    constexpr int kBuffers = 128;
    rxsim::VirtualInterface vi(kBuffers, 256);
    vi.post_all();

    std::atomic<bool> produce_done{false};
    std::uint64_t seen = 0;
    std::thread app([&] {
        rxsim::RxEvent evs[32]{};
        for (;;) {
            const int n = vi.poll(evs, 32);
            for (int i = 0; i < n; ++i) {
                require(vi.data(evs[i].buffer_id) != nullptr, "readable");
                require(vi.repost(evs[i].buffer_id), "repost");
                ++seen;
            }
            if (n == 0) {
                if (produce_done.load(std::memory_order_acquire) &&
                    vi.stats().completed.load() + vi.stats().rx_overrun.load() >=
                        static_cast<std::uint64_t>(kPackets) &&
                    seen >= vi.stats().completed.load()) {
                    break;
                }
                std::this_thread::yield();
            }
        }
    });

    rxsim::SimulatedNic nic(vi);
    nic.run_fake(static_cast<std::uint64_t>(kPackets), 16, /*yield_each=*/true,
                 /*wait_for_buffer=*/true);
    produce_done.store(true, std::memory_order_release);
    app.join();

    require(seen == static_cast<std::uint64_t>(kPackets), "app saw every packet");
    require(vi.stats().completed.load() == static_cast<std::uint64_t>(kPackets), "completed");
    require(vi.stats().rx_overrun.load() == 0, "keeping up means no overrun");
}

void test_cannot_read_posted_buffer() {
    rxsim::VirtualInterface vi(2, 128);
    require(vi.post(0), "post 0");
    require(vi.data(0) == nullptr, "must not read while NIC owns the buffer");
}

}  // namespace

int main() {
    try {
        test_post_poll_repost();
        test_overflow_without_consumer();
        test_larger_ring_absorbs_burst_then_sustained_drop();
        test_concurrent_consumer_keeps_up();
        test_cannot_read_posted_buffer();
    } catch (const std::exception& ex) {
        std::cerr << "test_ownership FAILED: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "test_ownership OK\n";
    return 0;
}
