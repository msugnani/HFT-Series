#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace hft {

// 64 bytes is the x86-64 cache-line size. We do not use
// std::hardware_destructive_interference_size — GCC can emit
// -Winterference-size (fatal under -Werror) because that constant is
// ABI-sensitive to -march.
inline constexpr std::size_t kCacheLine = 64;

inline std::uint64_t now_ns() {
#ifdef __linux__
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
#else
    const auto t = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
#endif
}

// Unserialized timestamp counter. Fine for short intervals on the same core.
// Do not compare TSC values across cores without checking invariance, and do
// not treat the delta as nanoseconds until you calibrate (see 04-linux-latency).
inline std::uint64_t rdtsc() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
    unsigned int lo = 0;
    unsigned int hi = 0;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
    return now_ns();
#endif
}

inline void cpu_relax() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause" ::: "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

template <typename T>
inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    volatile char const* p = reinterpret_cast<char const*>(&value);
    (void)*p;
#endif
}

inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

// Returns false if pinning is not supported or the call failed.
inline bool pin_cpu(int cpu) {
#ifdef __linux__
    if (cpu < 0) {
        return true;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    (void)cpu;
    return false;
#endif
}

inline void spin_for_ns(std::uint64_t ns) {
    const std::uint64_t until = now_ns() + ns;
    while (now_ns() < until) {
        cpu_relax();
    }
}

struct Argv {
    int argc;
    char** argv;

    bool has(std::string_view key) const {
        for (int i = 1; i < argc; ++i) {
            if (key == argv[i]) {
                return true;
            }
        }
        return false;
    }

    std::string get(std::string_view key, std::string_view fallback) const {
        for (int i = 1; i + 1 < argc; ++i) {
            if (key == argv[i]) {
                return argv[i + 1];
            }
        }
        return std::string(fallback);
    }

    std::uint64_t u64(std::string_view key, std::uint64_t fallback) const {
        const std::string s = get(key, "");
        if (s.empty()) {
            return fallback;
        }
        return static_cast<std::uint64_t>(std::strtoull(s.c_str(), nullptr, 10));
    }

    int i32(std::string_view key, int fallback) const {
        return static_cast<int>(u64(key, static_cast<std::uint64_t>(fallback)));
    }
};

// Log2 histogram in nanoseconds. Mean is reported; decisions should use
// percentiles. Bucket i holds samples in [2^(i-1), 2^i) ns for i > 0.
class Hist {
public:
    void add_ns(std::uint64_t ns) {
        ++count_;
        sum_ns_ += ns;
        if (ns < min_ns_) {
            min_ns_ = ns;
        }
        if (ns > max_ns_) {
            max_ns_ = ns;
        }
        ++buckets_[bucket(ns)];
    }

    [[nodiscard]] std::uint64_t count() const { return count_; }
    [[nodiscard]] std::uint64_t min_ns() const { return count_ == 0 ? 0 : min_ns_; }
    [[nodiscard]] std::uint64_t max_ns() const { return max_ns_; }

    [[nodiscard]] double avg_ns() const {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_ns_) / static_cast<double>(count_);
    }

    [[nodiscard]] std::uint64_t percentile_ns(double p) const {
        if (count_ == 0) {
            return 0;
        }
        const std::uint64_t target =
            static_cast<std::uint64_t>(static_cast<double>(count_ - 1) * p);
        std::uint64_t acc = 0;
        for (int i = 0; i < kBuckets; ++i) {
            acc += buckets_[i];
            if (acc > target) {
                return bucket_ns(i);
            }
        }
        return bucket_ns(kBuckets - 1);
    }

    void print(std::string_view name) const {
        std::cout << name << " n=" << count_ << " avg_ns=" << avg_ns()
                  << " p50_ns=" << percentile_ns(0.50) << " p99_ns=" << percentile_ns(0.99)
                  << " p999_ns=" << percentile_ns(0.999) << " min_ns=" << min_ns()
                  << " max_ns=" << max_ns() << '\n';
    }

private:
    static constexpr int kBuckets = 64;

    static int bucket(std::uint64_t ns) {
        int b = 0;
        std::uint64_t v = ns;
        while (v > 1 && b < kBuckets - 1) {
            v >>= 1;
            ++b;
        }
        return b;
    }

    static std::uint64_t bucket_ns(int b) { return b == 0 ? 0 : (1ull << b); }

    std::uint64_t count_{0};
    std::uint64_t sum_ns_{0};
    std::uint64_t min_ns_{~0ull};
    std::uint64_t max_ns_{0};
    std::uint64_t buckets_[kBuckets]{};
};

inline void print_affinity_hint() {
    const unsigned n = std::thread::hardware_concurrency();
    std::cout << "hardware_concurrency=" << n << '\n';
#ifndef __linux__
    std::cout << "pin_cpu() is a no-op on this OS; run the Linux labs on WSL2 "
                 "or a Linux box to pin producer/consumer onto two cores.\n";
#endif
}

}  // namespace hft
