#include "hft/common.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 20000000);
    const int random_mode = args.i32("--random", 0);

    std::vector<std::uint8_t> pred(static_cast<std::size_t>(iters));
    if (random_mode) {
        std::mt19937 rng(1);
        std::uniform_int_distribution<int> dist(0, 1);
        for (auto& v : pred) {
            v = static_cast<std::uint8_t>(dist(rng));
        }
    } else {
        for (std::size_t i = 0; i < pred.size(); ++i) {
            pred[i] = static_cast<std::uint8_t>(i & 1);
        }
    }

    std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < 100000 && i < iters; ++i) {
        sink += pred[static_cast<std::size_t>(i)] ? i : i ^ 1;
    }

    const std::uint64_t t0 = hft::now_ns();
    for (std::uint64_t i = 0; i < iters; ++i) {
        if (pred[static_cast<std::size_t>(i)]) {
            sink += i;
        } else {
            sink ^= i;
        }
    }
    const std::uint64_t t1 = hft::now_ns();
    hft::do_not_optimize(sink);

    std::cout << "iters=" << iters << " random=" << random_mode << " elapsed_ns=" << (t1 - t0)
              << " ns_per=" << (static_cast<double>(t1 - t0) / static_cast<double>(iters))
              << " sink=" << sink << '\n';
    std::cout << "run under: perf stat -e branches,branch-misses,cycles,instructions\n";
    return 0;
}
