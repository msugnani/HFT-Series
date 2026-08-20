# HFT Series

Notes and small C++ programs on low-latency systems. Study **in order**. Each chapter is a README plus a few binaries; run them in Release and, for pinning/`perf`, on Linux or WSL2.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Visual Studio puts apps under `build/<chapter>/Release/`. Ninja/Make puts them under `build/<chapter>/`.

| Ch | Topic | Directory |
| --- | --- | --- |
| 1 | Cache lines, false sharing, AoS/SoA, strides | [01-cache](01-cache) |
| 2 | C++ memory model, acquire/release, SPSC | [02-memory-model](02-memory-model) |
| 3 | C++17/20 cost model on the hot path | [03-cpp-cost-model](03-cpp-cost-model) |
| 4 | Linux jitter: clocks, faults, syscalls, pinning | [04-linux-latency](04-linux-latency) |
| 5 | Measurement hygiene, histograms, `perf` | [05-profiling](05-profiling) |
| 6 | Pools, batching, SPSC logging, in-place parse | [06-hot-path](06-hot-path) |
| 7 | Price-time limit order book | [07-order-book](07-order-book) |
| 8 | Userspace NIC / RX-ring model (ef_vi, ExaNIC) | [kernel-bypass](kernel-bypass) |

Shared headers live in [common/include/hft](common/include/hft): timing, pinning, histogram, `SpscRing`, `Pool`.

## How to use this for interview prep

Read the chapter README, run the commands in it, then answer the questions at the bottom **out loud** with numbers from your machine. The job description is those topics; a passing answer sounds like: “I measured it, here is p99, here is the counter that moved.”

Chapters 1–3, 6 and 7 are portable. Chapters 4–5 and 8 want Linux for the interesting results (`pin_cpu`, `perf`, UDP `recvmmsg`).
