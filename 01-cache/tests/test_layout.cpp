#include "hft/common.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

struct Unpadded {
    std::atomic<std::uint64_t> a;
    std::atomic<std::uint64_t> b;
};

struct Padded {
    alignas(hft::kCacheLine) std::atomic<std::uint64_t> a;
    alignas(hft::kCacheLine) std::atomic<std::uint64_t> b;
};

}  // namespace

int main() {
    try {
        require(hft::kCacheLine == 64, "x86-64 line size used throughout the series");
        require(sizeof(Unpadded) <= 16, "unpadded atomics should pack onto one line");
        Padded p{};
        require(sizeof(Padded) >= 2 * hft::kCacheLine, "each padded counter owns a line");
        const auto delta = reinterpret_cast<std::uintptr_t>(&p.b) -
                           reinterpret_cast<std::uintptr_t>(&p.a);
        require(delta >= hft::kCacheLine, "b must start on the next line");
    } catch (const std::exception& ex) {
        std::cerr << "test_layout FAILED: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "test_layout OK\n";
    return 0;
}
