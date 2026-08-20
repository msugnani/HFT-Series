#include "hft/common.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

// Work per item is a handful of adds. The "doorbell" is an atomic store that
// stands in for MMIO / a syscall. Batching amortizes that store.

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto n = args.u64("--n", 4000000);
    const auto batch = args.u64("--batch", 32);

    if (batch == 0) {
        std::cerr << "--batch must be > 0\n";
        return 1;
    }

    std::vector<std::uint64_t> px(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) {
        px[static_cast<std::size_t>(i)] = i + 1;
    }

    alignas(hft::kCacheLine) std::atomic<std::uint64_t> doorbell{0};

    auto run = [&](std::uint64_t b) {
        std::uint64_t sum = 0;
        std::uint64_t pending = 0;
        doorbell.store(0, std::memory_order_relaxed);
        const std::uint64_t t0 = hft::now_ns();
        for (std::uint64_t i = 0; i < n; ++i) {
            sum += px[static_cast<std::size_t>(i)];
            ++pending;
            if (pending == b) {
                doorbell.store(i + 1, std::memory_order_release);
                pending = 0;
            }
        }
        if (pending != 0) {
            doorbell.store(n, std::memory_order_release);
        }
        const std::uint64_t t1 = hft::now_ns();
        hft::do_not_optimize(sum);
        hft::do_not_optimize(doorbell);
        return std::pair<std::uint64_t, std::uint64_t>{t1 - t0, sum};
    };

    (void)run(batch);

    const auto result = run(batch);
    std::cout << "n=" << n << " batch=" << batch << " elapsed_ns=" << result.first
              << " ns_per=" << (static_cast<double>(result.first) / static_cast<double>(n))
              << " doorbell=" << doorbell.load() << " checksum=" << result.second << '\n';
    return 0;
}
