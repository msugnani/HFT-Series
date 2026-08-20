#include "hft/common.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto samples = args.u64("--samples", 100000);
    const int cpu = args.i32("--cpu", -1);
    const auto sleep_us = args.u64("--sleep-us", 0);

    if (cpu >= 0) {
        if (!hft::pin_cpu(cpu)) {
            std::cerr << "pin_cpu(" << cpu << ") failed or unsupported\n";
        }
    }
    hft::print_affinity_hint();

    // Unmeasured warmup so we are not timing thread start.
    for (int i = 0; i < 10000; ++i) {
        hft::cpu_relax();
    }

    hft::Hist hist;
    std::uint64_t prev = hft::now_ns();
    for (std::uint64_t i = 0; i < samples; ++i) {
        if (sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        } else {
            hft::cpu_relax();
        }
        const std::uint64_t now = hft::now_ns();
        hist.add_ns(now - prev);
        prev = now;
    }

    std::cout << "samples=" << samples << " cpu=" << cpu << " sleep_us=" << sleep_us << '\n';
    hist.print("intersample");
    return 0;
}
