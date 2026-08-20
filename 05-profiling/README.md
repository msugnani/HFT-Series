# Profiling and measurement

You cannot tune what you have not measured. Mean latency is a vanity number; **p99 / p99.9 / max** are the job. This chapter is a lab manual for the binaries in chapters 01–04, plus two small programs that make `perf` and a histogram argue with each other.

## Hygiene

1. **Release**, LTO if you have it, same `-march` as production. Debug benches are fiction.
2. **Warmup.** First-touch faults (ch. 04) and I-cache misses are not the steady poll loop.
3. **Pin** the threads you care about. Cross-core migration looks like jitter.
4. **Do not log** in the timed region. Logging is a different path (ch. 06).
5. Report **n, p50, p99, p999, max**, not “about 20 ns.”
6. Separate **throughput** (items/s) from **latency** (time in the system). A ring that is always full has great throughput and terrible latency.

`hft::Hist` is a log2 histogram in nanoseconds. Bucket edges are powers of two, so treat p99 as order-of-magnitude, not a 1 ns claim. For publication-quality plots you would keep a denser histogram or a HDR histogram. For interview prep, this is enough to stop quoting means.

## Tools

| Tool | Use |
| --- | --- |
| `perf stat` | counters: cycles, instructions, IPC, L1/LLC, dTLB, branches |
| `perf record` / `perf report` | where the cycles went |
| `perf c2c` | false sharing (chapter 01) |
| flame graph | `perf script` → stack collapse → svg |
| `/proc/interrupts` | who is interrupting your core |
| `numactl` / `taskset` | placement |
| ASan/TSan/UBSan | correctness, **off** the numbered run |
| VTune / Visual Studio profiler | if you are stuck on Windows without `perf` |

Linux (WSL2 with `perf` or a real box):

```bash
perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses,dTLB-load-misses,branch-misses \
  ./build/01-cache/false_sharing --iters 50000000 --padded 0 --cpu-a 0 --cpu-b 1

perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses \
  ./build/01-cache/false_sharing --iters 50000000 --padded 1 --cpu-a 0 --cpu-b 1
```

You are looking for **cycles per increment**, not a green dashboard. Unpadded should spend cycles in coherence, not in `fetch_add`.

```bash
perf record -g ./build/03-cpp-cost-model/hashmap_vs_array --lookups 8000000
perf report
```

TSan for the ring (expect it clean):

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
cmake --build build-tsan -j --target test_spsc_hft
./build-tsan/02-memory-model/test_spsc_hft
```

## Examples

```bash
./build/05-profiling/measure --iters 200000 --warmup 20000 --work 64
./build/05-profiling/branchy --iters 20000000 --random 0
./build/05-profiling/branchy --iters 20000000 --random 1
```

### 1. `measure`

Times one call that touches `--work` cache lines. Prints a histogram. `--warmup` is discarded. Use this as the skeleton for any microbench you add: pin, warmup, histogram, `do_not_optimize`.

### 2. `branchy`

A predictable branch vs a random one. `perf stat -e branches,branch-misses` should show the random mode. This is why market-data parsers lean on table-driven tags and why “clever” `if` in the tick path shows up in p99.

## Reading a result

```text
intersample n=200000 avg_ns=38 p50_ns=32 p99_ns=64 p999_ns=256 max_ns=48000
```

p50 is the spin. p999/max is the OS. If max is tens of microseconds on a supposedly isolated core, you are not isolated: IRQs, SMT, or the scheduler still own you.

## Questions to close this chapter

1. Why can IPC go *up* while p99 gets worse? (You started doing more work per item, or you batched.)
2. Why is `perf` on a debug build worse than useless?
3. You removed a `std::shared_ptr` from the poll loop and p99 dropped. Which counters do you expect to move (`cache-misses` vs `branch-misses` vs atomic ops)?
4. Re-run chapter 01 under `perf c2c` and explain one HITM line.
