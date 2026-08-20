#include "hft/common.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

// Two counters on one line vs two counters on two lines.
// The increment work is identical. The coherence traffic is not.

namespace {

struct Unpadded {
    std::atomic<std::uint64_t> a{0};
    std::atomic<std::uint64_t> b{0};
};

struct Padded {
    alignas(hft::kCacheLine) std::atomic<std::uint64_t> a{0};
    alignas(hft::kCacheLine) std::atomic<std::uint64_t> b{0};
};

template <typename Pair>
std::uint64_t run(Pair& pair, std::uint64_t iters, int cpu_a, int cpu_b) {
    const std::uint64_t t0 = hft::now_ns();
    std::thread ta([&] {
        (void)hft::pin_cpu(cpu_a);
        for (std::uint64_t i = 0; i < iters; ++i) {
            pair.a.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread tb([&] {
        (void)hft::pin_cpu(cpu_b);
        for (std::uint64_t i = 0; i < iters; ++i) {
            pair.b.fetch_add(1, std::memory_order_relaxed);
        }
    });
    ta.join();
    tb.join();
    return hft::now_ns() - t0;
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 20000000);
    const int padded = args.i32("--padded", 1);
    const int cpu_a = args.i32("--cpu-a", 0);
    const int cpu_b = args.i32("--cpu-b", 1);

    hft::print_affinity_hint();
    std::cout << "iters=" << iters << " padded=" << padded << " cpu_a=" << cpu_a
              << " cpu_b=" << cpu_b << '\n';
    std::cout << "sizeof(Unpadded)=" << sizeof(Unpadded) << " sizeof(Padded)=" << sizeof(Padded)
              << '\n';

    std::uint64_t elapsed = 0;
    if (padded) {
        Padded pair;
        elapsed = run(pair, iters, cpu_a, cpu_b);
        hft::do_not_optimize(pair.a);
        hft::do_not_optimize(pair.b);
    } else {
        Unpadded pair;
        elapsed = run(pair, iters, cpu_a, cpu_b);
        hft::do_not_optimize(pair.a);
        hft::do_not_optimize(pair.b);
    }

    const double ns_per = static_cast<double>(elapsed) / static_cast<double>(iters);
    std::cout << "elapsed_ns=" << elapsed << " ns_per_increment=" << ns_per << '\n';
    return 0;
}
