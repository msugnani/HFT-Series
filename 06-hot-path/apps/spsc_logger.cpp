#include "hft/common.hpp"
#include "hft/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

struct LogRec {
    std::uint64_t seq;
    std::uint64_t tsc;
    std::uint32_t code;
    std::uint32_t a;
};

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 1000000);
    const auto cap = static_cast<std::size_t>(args.u64("--cap", 4096));
    const int use_mutex = args.i32("--mutex", 0);
    const int drop = args.i32("--drop", 1);
    const int cpu_hot = args.i32("--cpu-hot", 0);
    const int cpu_log = args.i32("--cpu-log", 1);

    hft::print_affinity_hint();
    std::cout << "iters=" << iters << " cap=" << cap << " mutex=" << use_mutex
              << " drop=" << drop << '\n';

    std::uint64_t dropped = 0;
    std::uint64_t logged = 0;
    std::uint64_t sink = 0;

    const std::uint64_t t0 = hft::now_ns();

    if (use_mutex) {
        std::mutex mu;
        std::deque<LogRec> q;
        std::atomic<bool> done{false};

        std::thread logger([&] {
            (void)hft::pin_cpu(cpu_log);
            for (;;) {
                LogRec r{};
                {
                    std::lock_guard<std::mutex> lock(mu);
                    if (q.empty()) {
                        if (done.load(std::memory_order_acquire)) {
                            break;
                        }
                        continue;
                    }
                    r = q.front();
                    q.pop_front();
                }
                sink ^= r.seq ^ r.tsc;
                ++logged;
            }
        });

        (void)hft::pin_cpu(cpu_hot);
        for (std::uint64_t i = 1; i <= iters; ++i) {
            LogRec r{i, hft::rdtsc(), 1, static_cast<std::uint32_t>(i)};
            {
                std::lock_guard<std::mutex> lock(mu);
                q.push_back(r);
            }
        }
        done.store(true, std::memory_order_release);
        logger.join();
    } else {
        hft::SpscRing<LogRec> ring(cap);
        std::atomic<bool> done{false};

        std::thread logger([&] {
            (void)hft::pin_cpu(cpu_log);
            LogRec r{};
            for (;;) {
                if (ring.try_pop(r)) {
                    sink ^= r.seq ^ r.tsc;
                    ++logged;
                    continue;
                }
                if (done.load(std::memory_order_acquire) && ring.empty()) {
                    break;
                }
                hft::cpu_relax();
            }
        });

        (void)hft::pin_cpu(cpu_hot);
        for (std::uint64_t i = 1; i <= iters; ++i) {
            const LogRec r{i, hft::rdtsc(), 1, static_cast<std::uint32_t>(i)};
            if (drop) {
                if (!ring.try_push(r)) {
                    ++dropped;
                }
            } else {
                while (!ring.try_push(r)) {
                    hft::cpu_relax();
                }
            }
        }
        done.store(true, std::memory_order_release);
        logger.join();
    }

    const std::uint64_t t1 = hft::now_ns();
    hft::do_not_optimize(sink);
    std::cout << "elapsed_ns=" << (t1 - t0) << " logged=" << logged << " dropped=" << dropped
              << " sink=" << sink << '\n';
    return 0;
}
