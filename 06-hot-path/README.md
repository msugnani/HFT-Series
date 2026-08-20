# Hot-path patterns

The pieces from chapters 01–05 assemble into a small number of shapes that show up in every low-latency datapath: **preallocate**, **ring**, **batch**, **zero-copy**, **poll**, **log off to the side**.

[kernel-bypass](../kernel-bypass) is the networked version of the same shapes (descriptor ring + event ring + packet pool). This chapter keeps the CPU on one box so you can see batching, pooling, and side logging without sockets.

## Pattern table

| Pattern | Problem it kills |
| --- | --- |
| Pool at startup | `new` in the poll loop (ch. 03) |
| SPSC ring | Shared mutex, false sharing on a queue head |
| Batch / doorbell | Per-item syscall, per-item fence, per-item branch |
| Zero-copy | `memcpy` of payload the NIC already wrote |
| Busy poll, isolated core | Scheduler jitter (ch. 04) |
| SPSC logger | Formatting and I/O on the book thread |

```mermaid
flowchart LR
  hot[Hot_thread]
  ring[SPSC_log_ring]
  log[Logger_thread]
  hot -->|"POD try_push"| ring
  ring --> log
  log --> sink[checksum_or_disk]
```

If `try_push` fails, the hot thread **drops** (or sheds a level of detail). It does not block, allocate, or format. A stall in logging must not stall matching.

## Examples

```bash
./build/06-hot-path/batching --n 8000000 --batch 1
./build/06-hot-path/batching --n 8000000 --batch 32
./build/06-hot-path/spsc_logger --iters 2000000 --cap 4096 --drop 1
./build/06-hot-path/spsc_logger --iters 2000000 --cap 4096 --drop 0
./build/06-hot-path/spsc_logger --iters 2000000 --mutex 1
./build/06-hot-path/parse --n 500000 --copy 0
./build/06-hot-path/parse --n 500000 --copy 1
```

### 1. Batching

Each item does a little work, then a `doorbell` (an atomic store standing in for a syscall or MMIO). `--batch 1` rings per item. `--batch 32` rings once per group. Amortisation is why `recvmmsg`, RX coalescing, and `ef_vi_receive_push` exist.

### 2. SPSC logger

The hot thread only writes a POD record. A second thread drains the ring. `--drop 1` never waits: a full ring increments `dropped` (lose logs, keep ticks). `--drop 0` spins until there is space (backpressure). `--mutex 1` takes a lock and pushes onto a `std::deque` on every log — that is how a “harmless” `std::cout` becomes p99.

If you chose “never block,” a slow logger must not stall matching. Make that product decision explicit.

### 3. Parse without heap

A packed length+type stream, walked with a pointer. `--copy 1` first copies each payload into a `std::string` — the control for “we just parse messages.” Production feed handlers look like `--copy 0`: bounds check, switch on type, read fields in place.

## Questions to close this chapter

1. Why is a completion ring + descriptor ring two SPSC queues, not one bidirectional queue?
2. What happens when the logger is slower than the book? (Drops, or the ring back-pressures, which is a bug if you chose “never block.”)
3. Where does SIMD belong? (Find-byte, checksum, vector-wide book updates — after the scalar path is allocation-free and measured.)
4. Connect this to kernel-bypass: `post` is acquire-a-buffer, `poll` is drain completions, `repost` is release-a-buffer. Same ownership loop as `hft::Pool`, with the NIC as the other thread.
