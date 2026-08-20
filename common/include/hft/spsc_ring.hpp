#pragma once

#include "hft/common.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace hft {

constexpr bool is_power_of_two(std::size_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

constexpr std::size_t next_power_of_two(std::size_t n) noexcept {
    if (n <= 1) {
        return 1;
    }
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    if constexpr (sizeof(std::size_t) > 4) {
        n |= n >> 32;
    }
    return n + 1;
}

// Lock-free single-producer / single-consumer ring.
// Walked line-by-line in 02-memory-model. The kernel-bypass chapter uses the
// same shape for NIC descriptor and event rings.
template <typename T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscRing stores T by copy; keep descriptors/events as POD");

public:
    explicit SpscRing(std::size_t capacity)
        : cap_(next_power_of_two(capacity)),
          mask_(cap_ - 1),
          slots_(std::make_unique<T[]>(cap_)) {
        if (capacity == 0) {
            throw std::invalid_argument("SpscRing capacity must be > 0");
        }
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return cap_; }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t w = write_.value.load(std::memory_order_relaxed);
        const std::size_t r = read_.value.load(std::memory_order_relaxed);
        return w - r;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() >= cap_; }

    // Producer only. Payload store is sequenced-before the release on write_.
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const std::size_t w = write_.value.load(std::memory_order_relaxed);
        const std::size_t r = read_.value.load(std::memory_order_acquire);
        if (w - r >= cap_) {
            return false;
        }
        slots_[w & mask_] = value;
        write_.value.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumer only. Acquire on write_ happens-before the payload load.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const std::size_t r = read_.value.load(std::memory_order_relaxed);
        const std::size_t w = write_.value.load(std::memory_order_acquire);
        if (r == w) {
            return false;
        }
        out = slots_[r & mask_];
        read_.value.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    struct alignas(kCacheLine) PaddedIndex {
        std::atomic<std::size_t> value{0};
    };

    const std::size_t cap_;
    const std::size_t mask_;
    std::unique_ptr<T[]> slots_;
    PaddedIndex write_;
    PaddedIndex read_;
};

}  // namespace hft
