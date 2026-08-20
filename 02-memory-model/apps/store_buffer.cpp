#include "hft/common.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

// Dekker / store-buffer experiment.
// Each thread stores its own flag then loads the other. Both reading 0 means
// the loads passed the stores — allowed for relaxed on x86 (store buffer),
// forbidden for seq_cst.

namespace {

alignas(hft::kCacheLine) std::atomic<int> x{0};
alignas(hft::kCacheLine) std::atomic<int> y{0};
alignas(hft::kCacheLine) std::atomic<int> r1{0};
alignas(hft::kCacheLine) std::atomic<int> r2{0};

enum class Phase : int { Idle = 0, Run = 1, Done = 2, Stop = 3 };

alignas(hft::kCacheLine) std::atomic<int> gate_a{0};
alignas(hft::kCacheLine) std::atomic<int> gate_b{0};

void worker_a(bool seqcst, int cpu) {
    (void)hft::pin_cpu(cpu);
    for (;;) {
        int g = 0;
        while ((g = gate_a.load(std::memory_order_acquire)) == static_cast<int>(Phase::Idle)) {
            hft::cpu_relax();
        }
        if (g == static_cast<int>(Phase::Stop)) {
            return;
        }
        if (seqcst) {
            x.store(1, std::memory_order_seq_cst);
            r1.store(y.load(std::memory_order_seq_cst), std::memory_order_relaxed);
        } else {
            x.store(1, std::memory_order_relaxed);
            r1.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        gate_a.store(static_cast<int>(Phase::Done), std::memory_order_release);
        while (gate_a.load(std::memory_order_acquire) == static_cast<int>(Phase::Done)) {
            hft::cpu_relax();
        }
    }
}

void worker_b(bool seqcst, int cpu) {
    (void)hft::pin_cpu(cpu);
    for (;;) {
        int g = 0;
        while ((g = gate_b.load(std::memory_order_acquire)) == static_cast<int>(Phase::Idle)) {
            hft::cpu_relax();
        }
        if (g == static_cast<int>(Phase::Stop)) {
            return;
        }
        if (seqcst) {
            y.store(1, std::memory_order_seq_cst);
            r2.store(x.load(std::memory_order_seq_cst), std::memory_order_relaxed);
        } else {
            y.store(1, std::memory_order_relaxed);
            r2.store(x.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        gate_b.store(static_cast<int>(Phase::Done), std::memory_order_release);
        while (gate_b.load(std::memory_order_acquire) == static_cast<int>(Phase::Done)) {
            hft::cpu_relax();
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 200000);
    const int seqcst = args.i32("--seqcst", 0);
    const int cpu_a = args.i32("--cpu-a", 0);
    const int cpu_b = args.i32("--cpu-b", 1);

    hft::print_affinity_hint();
    std::cout << "iters=" << iters << " seqcst=" << seqcst << '\n';

    std::thread ta(worker_a, seqcst != 0, cpu_a);
    std::thread tb(worker_b, seqcst != 0, cpu_b);

    std::uint64_t zeros = 0;
    for (std::uint64_t i = 0; i < iters; ++i) {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
        r1.store(0, std::memory_order_relaxed);
        r2.store(0, std::memory_order_relaxed);

        gate_a.store(static_cast<int>(Phase::Run), std::memory_order_release);
        gate_b.store(static_cast<int>(Phase::Run), std::memory_order_release);

        while (gate_a.load(std::memory_order_acquire) != static_cast<int>(Phase::Done)) {
            hft::cpu_relax();
        }
        while (gate_b.load(std::memory_order_acquire) != static_cast<int>(Phase::Done)) {
            hft::cpu_relax();
        }

        if (r1.load(std::memory_order_relaxed) == 0 && r2.load(std::memory_order_relaxed) == 0) {
            ++zeros;
        }

        gate_a.store(static_cast<int>(Phase::Idle), std::memory_order_release);
        gate_b.store(static_cast<int>(Phase::Idle), std::memory_order_release);
    }

    gate_a.store(static_cast<int>(Phase::Stop), std::memory_order_release);
    gate_b.store(static_cast<int>(Phase::Stop), std::memory_order_release);
    ta.join();
    tb.join();

    std::cout << "(0,0)_count=" << zeros << " rate="
              << (static_cast<double>(zeros) / static_cast<double>(iters)) << '\n';
    return 0;
}
