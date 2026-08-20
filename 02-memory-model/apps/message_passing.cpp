#include "hft/common.hpp"
#include "hft/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

// Correct: store payload, then release-publish the index.
// Wrong:   publish the index, then store payload — consumer can pop garbage.

namespace {

struct Msg {
    std::uint64_t seq;
    std::uint64_t payload;
};

struct SlotRing {
    explicit SlotRing(std::size_t requested)
        : cap(hft::next_power_of_two(requested)), mask(cap - 1), slots(cap) {}

    std::size_t cap;
    std::size_t mask;
    std::vector<Msg> slots;
    alignas(hft::kCacheLine) std::atomic<std::size_t> write{0};
    alignas(hft::kCacheLine) std::atomic<std::size_t> read{0};

    bool try_push(const Msg& m, bool wrong) {
        const std::size_t w = write.load(std::memory_order_relaxed);
        const std::size_t r = read.load(std::memory_order_acquire);
        if (w - r >= cap) {
            return false;
        }
        if (wrong) {
            write.store(w + 1, std::memory_order_release);
            slots[w & mask] = m;
        } else {
            slots[w & mask] = m;
            write.store(w + 1, std::memory_order_release);
        }
        return true;
    }

    bool try_pop(Msg& out) {
        const std::size_t r = read.load(std::memory_order_relaxed);
        const std::size_t w = write.load(std::memory_order_acquire);
        if (r == w) {
            return false;
        }
        out = slots[r & mask];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 1000000);
    const int wrong = args.i32("--wrong", 0);
    const auto cap = static_cast<std::size_t>(args.u64("--cap", 1024));
    const int cpu_a = args.i32("--cpu-a", 0);
    const int cpu_b = args.i32("--cpu-b", 1);

    hft::print_affinity_hint();
    std::cout << "iters=" << iters << " wrong=" << wrong << " cap=" << cap << '\n';

    SlotRing ring(cap);
    std::atomic<std::uint64_t> mismatches{0};
    std::atomic<std::uint64_t> consumed{0};

    std::thread producer([&] {
        (void)hft::pin_cpu(cpu_a);
        for (std::uint64_t i = 1; i <= iters; ++i) {
            const Msg m{i, i * 111};
            while (!ring.try_push(m, wrong != 0)) {
                hft::cpu_relax();
            }
        }
    });

    std::thread consumer([&] {
        (void)hft::pin_cpu(cpu_b);
        Msg m{};
        while (consumed.load(std::memory_order_relaxed) < iters) {
            if (!ring.try_pop(m)) {
                hft::cpu_relax();
                continue;
            }
            consumed.fetch_add(1, std::memory_order_relaxed);
            if (m.payload != m.seq * 111) {
                mismatches.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "consumed=" << consumed.load() << " mismatches=" << mismatches.load() << '\n';
    if (!wrong && mismatches.load() != 0) {
        std::cerr << "correct protocol must not mismatch\n";
        return 1;
    }
    return 0;
}
