#include "hft/common.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

// Skeleton microbench: warmup, pin, histogram, prevent elision.

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 100000);
    const auto warmup = args.u64("--warmup", 10000);
    const auto work = static_cast<std::size_t>(args.u64("--work", 64));
    const int cpu = args.i32("--cpu", -1);

    if (cpu >= 0 && !hft::pin_cpu(cpu)) {
        std::cerr << "pin_cpu failed or unsupported\n";
    }
    hft::print_affinity_hint();

    std::vector<std::uint64_t> line(work * (hft::kCacheLine / sizeof(std::uint64_t)), 1);

    auto once = [&] {
        std::uint64_t s = 0;
        for (std::size_t i = 0; i < line.size(); i += hft::kCacheLine / sizeof(std::uint64_t)) {
            s += line[i];
        }
        hft::do_not_optimize(s);
        return s;
    };

    std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < warmup; ++i) {
        sink ^= once();
    }

    hft::Hist hist;
    for (std::uint64_t i = 0; i < iters; ++i) {
        const std::uint64_t t0 = hft::now_ns();
        sink ^= once();
        hist.add_ns(hft::now_ns() - t0);
    }

    hft::do_not_optimize(sink);
    std::cout << "iters=" << iters << " warmup=" << warmup << " work_lines=" << work
              << " sink=" << sink << '\n';
    hist.print("touch_lines");
    return 0;
}
