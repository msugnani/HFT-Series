#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rxsim {

// 64 bytes is the x86-64 cache-line size. We do not use
// std::hardware_destructive_interference_size — GCC can emit
// -Winterference-size (fatal under -Werror) because that constant is
// ABI-sensitive to -march.
#ifndef RXSIM_CACHE_LINE
inline constexpr std::size_t kCacheLine = 64;
#else
inline constexpr std::size_t kCacheLine = RXSIM_CACHE_LINE;
#endif

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
//
// Why head and tail live on separate cache lines
// ----------------------------------------------
// The producer only writes `write_` (and reads `read_` to detect full).
// The consumer only writes `read_`  (and reads `write_` to detect empty).
// If those two atomics shared a cache line, every publish would invalidate
// the consumer's line and every consume would invalidate the producer's —
// classic false sharing. Padding them to kCacheLine makes the hot path
// look like two independent producer-consumer counters, which is the same
// reason real NIC descriptor/completion rings isolate doorbell vs. consumer
// indices.
//
// Memory ordering
// ---------------
// publish: store slot, then write_.store(release)
// consume: write_.load(acquire), then load slot, then read_.store(release)
// The acquire/release pair is what makes the payload visible across threads
// (and, on a real NIC, is the software analogue of a DMA completion barrier).
//
// Capacity is rounded up to a power of two so indexing is a mask, not a
// divide. The ring stores up to `capacity()` items (write - read == cap
// means full). One side of this queue will later be a SimulatedNic thread;
// conceptually that thread is standing in for DMA hardware.
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

    // Approximate occupied count. Safe for stats; not a linearization point
    // you should use for flow control (use try_push / try_pop).
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t w = write_.value.load(std::memory_order_relaxed);
        const std::size_t r = read_.value.load(std::memory_order_relaxed);
        return w - r;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool full() const noexcept { return size() >= cap_; }

    // Producer only.
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

    // Consumer only.
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

}  // namespace rxsim
