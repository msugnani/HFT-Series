#include "hft/common.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

#ifdef __linux__
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 1000000);

    auto bench = [&](auto&& fn, const char* name) {
        std::uint64_t sink = 0;
        for (std::uint64_t i = 0; i < 10000; ++i) {
            sink ^= fn();
        }
        const std::uint64_t t0 = hft::now_ns();
        for (std::uint64_t i = 0; i < iters; ++i) {
            sink ^= fn();
        }
        const std::uint64_t t1 = hft::now_ns();
        hft::do_not_optimize(sink);
        const double ns = static_cast<double>(t1 - t0) / static_cast<double>(iters);
        std::cout << name << " ns_per=" << ns << " elapsed_ns=" << (t1 - t0) << " sink=" << sink
                  << '\n';
    };

    bench([] { return hft::rdtsc(); }, "rdtsc");
    bench([] { return hft::now_ns(); }, "now_ns");
    bench(
        [] {
            const auto t = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
        },
        "chrono_steady");

#ifdef __linux__
    bench([] { return static_cast<std::uint64_t>(getppid()); }, "syscall_getppid");
#else
    std::cout << "syscall_getppid skipped (Linux only)\n";
#endif

    const std::uint64_t tsc0 = hft::rdtsc();
    const std::uint64_t ns0 = hft::now_ns();
    hft::spin_for_ns(50'000'000);
    const std::uint64_t tsc1 = hft::rdtsc();
    const std::uint64_t ns1 = hft::now_ns();
    const double tsc_hz =
        static_cast<double>(tsc1 - tsc0) / (static_cast<double>(ns1 - ns0) / 1e9);
    std::cout << "tsc_calibrated_hz~=" << tsc_hz << '\n';
    return 0;
}
