#pragma once

#include "rxsim/packet.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

inline std::uint64_t now_ns() {
#ifdef __linux__
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
#else
    const auto t = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t).count());
#endif
}

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

inline void spin_for_us(std::uint32_t us) {
    if (us == 0) {
        return;
    }
    const std::uint64_t until = now_ns() + static_cast<std::uint64_t>(us) * 1000ull;
    while (now_ns() < until) {
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

class LatencyHist {
public:
    void add_us(std::uint64_t us) {
        ++count_;
        sum_us_ += us;
        const int b = bucket(us);
        ++buckets_[b];
    }

    [[nodiscard]] std::uint64_t count() const { return count_; }

    [[nodiscard]] double avg_us() const {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_us_) / static_cast<double>(count_);
    }

    [[nodiscard]] std::uint64_t percentile_us(double p) const {
        if (count_ == 0) {
            return 0;
        }
        const std::uint64_t target =
            static_cast<std::uint64_t>(static_cast<double>(count_ - 1) * p);
        std::uint64_t acc = 0;
        for (int i = 0; i < kBuckets; ++i) {
            acc += buckets_[i];
            if (acc > target) {
                return bucket_us(i);
            }
        }
        return bucket_us(kBuckets - 1);
    }

private:
    static constexpr int kBuckets = 64;

    static int bucket(std::uint64_t us) {
        int b = 0;
        std::uint64_t v = us;
        while (v > 1 && b < kBuckets - 1) {
            v >>= 1;
            ++b;
        }
        return b;
    }

    static std::uint64_t bucket_us(int b) { return b == 0 ? 0 : (1ull << b); }

    std::uint64_t count_{0};
    std::uint64_t sum_us_{0};
    std::uint64_t buckets_[kBuckets]{};
};
