#include "hft/common.hpp"

#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

// Session start: map symbology to a dense id. Hot path: index an array.

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto lookups = args.u64("--lookups", 4000000);
    const auto ids = static_cast<std::size_t>(args.u64("--ids", 256));

    std::unordered_map<std::uint32_t, double> map;
    map.reserve(ids);
    std::vector<double> table(ids);
    std::vector<std::uint32_t> keys(static_cast<std::size_t>(lookups));

    for (std::size_t i = 0; i < ids; ++i) {
        const auto id = static_cast<std::uint32_t>(i);
        const double px = static_cast<double>(i) + 0.5;
        map[id] = px;
        table[i] = px;
    }
    for (std::uint64_t i = 0; i < lookups; ++i) {
        keys[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(i % ids);
    }

    double sink = 0;
    for (std::uint32_t k : keys) {
        sink += map[k] + table[k];
    }

    const std::uint64_t t0 = hft::now_ns();
    for (std::uint32_t k : keys) {
        sink += map.find(k)->second;
    }
    const std::uint64_t t1 = hft::now_ns();
    for (std::uint32_t k : keys) {
        sink += table[k];
    }
    const std::uint64_t t2 = hft::now_ns();

    hft::do_not_optimize(sink);
    std::cout << "lookups=" << lookups << " ids=" << ids << " sink=" << sink << '\n';
    std::cout << "unordered_map_ns=" << (t1 - t0) << " array_ns=" << (t2 - t1) << '\n';
    std::cout << "ns_per_map="
              << (static_cast<double>(t1 - t0) / static_cast<double>(lookups))
              << " ns_per_array="
              << (static_cast<double>(t2 - t1) / static_cast<double>(lookups)) << '\n';
    return 0;
}
