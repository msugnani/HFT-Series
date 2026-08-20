#include "hft/common.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// Sequential access rides the prefetcher. Large strides and random indexes
// miss L1 and then the TLB. Same add, different memory.

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto n = static_cast<std::size_t>(args.u64("--n", 1 << 24));
    const auto stride = static_cast<std::size_t>(args.u64("--stride", 1));
    const int random_mode = args.i32("--random", 0);

    if (stride == 0) {
        std::cerr << "--stride must be > 0\n";
        return 1;
    }

    std::vector<std::uint64_t> data(n);
    std::iota(data.begin(), data.end(), 1);

    std::vector<std::size_t> index;
    if (random_mode) {
        index.resize(n);
        std::iota(index.begin(), index.end(), 0);
        std::mt19937_64 rng(1);
        std::shuffle(index.begin(), index.end(), rng);
    }

    auto sweep = [&] {
        std::uint64_t sum = 0;
        if (random_mode) {
            for (std::size_t i : index) {
                sum += data[i];
            }
        } else {
            for (std::size_t i = 0; i < n; i += stride) {
                sum += data[i];
            }
        }
        return sum;
    };

    hft::do_not_optimize(sweep());

    const std::uint64_t t0 = hft::now_ns();
    const std::uint64_t sum = sweep();
    const std::uint64_t t1 = hft::now_ns();
    hft::do_not_optimize(sum);

    const std::size_t touches = random_mode ? n : (n + stride - 1) / stride;
    std::cout << "n=" << n << " stride=" << stride << " random=" << random_mode
              << " touches=" << touches << '\n';
    std::cout << "elapsed_ns=" << (t1 - t0) << " ns_per_touch="
              << (static_cast<double>(t1 - t0) / static_cast<double>(touches))
              << " checksum=" << sum << '\n';
    return 0;
}
