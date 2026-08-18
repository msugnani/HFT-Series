#include "common.hpp"

#include "rxsim/simulated_nic.hpp"
#include "rxsim/virtual_interface.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    const Argv args{argc, argv};
    if (args.has("--help")) {
        std::cout
            << "receiver --port 9000 --buffers 64 --payload 2048\n"
            << "         [--cpu 2] [--slow-us 0] [--seconds 0] [--batch 32]\n"
            << "         busy-polls the event ring; --slow-us simulates a slow app\n";
        return 0;
    }

#ifndef __linux__
    std::cerr << "receiver requires Linux/WSL\n";
    return 1;
#else
    const std::uint16_t port = static_cast<std::uint16_t>(args.u64("--port", 9000));
    const std::size_t buffers = static_cast<std::size_t>(args.u64("--buffers", 64));
    const std::size_t bufsz = static_cast<std::size_t>(args.u64("--payload", 2048));
    const int cpu = args.i32("--cpu", -1);
    const std::uint32_t slow_us = static_cast<std::uint32_t>(args.u64("--slow-us", 0));
    const std::uint64_t seconds = args.u64("--seconds", 0);
    const int batch = args.i32("--batch", 32);

    if (!pin_cpu(cpu)) {
        std::cerr << "warning: failed to pin CPU " << cpu << '\n';
    }

    rxsim::VirtualInterface vi(buffers, bufsz);
    const std::size_t posted = vi.post_all();
    rxsim::SimulatedNic nic(vi);
    nic.start_udp(port, batch);

    std::cout << "listening on UDP :" << port << " buffers=" << posted
              << " ring_cap=" << buffers << '\n';

    rxsim::RxEvent evs[64]{};
    LatencyHist hist;
    std::uint64_t expected = 0;
    std::uint64_t gaps = 0;
    std::uint64_t last_print = now_ns();
    std::uint64_t last_rx = 0;
    const std::uint64_t t0 = now_ns();
    const std::uint64_t deadline = seconds == 0 ? 0 : t0 + seconds * 1000000000ull;

    while (deadline == 0 || now_ns() < deadline) {
        const int n = vi.poll(evs, 64);
        for (int i = 0; i < n; ++i) {
            const auto* raw = vi.data(evs[i].buffer_id);
            if (raw != nullptr && evs[i].length >= rxsim::kMinPayload) {
                rxsim::PacketHeader hdr{};
                std::memcpy(&hdr, raw, sizeof(hdr));
                if (hdr.seq > expected) {
                    gaps += hdr.seq - expected;
                }
                expected = hdr.seq + 1;
                if (hdr.timestamp_ns != 0 && hdr.timestamp_ns <= now_ns()) {
                    hist.add_us((now_ns() - hdr.timestamp_ns) / 1000);
                }
            }
            spin_for_us(slow_us);
            vi.repost(evs[i].buffer_id);
        }

        const std::uint64_t now = now_ns();
        if (now - last_print >= 1000000000ull) {
            const auto rx = vi.stats().completed.load();
            const auto drop = vi.stats().rx_overrun.load();
            std::cout << "rx=" << rx << " (+" << (rx - last_rx) << "/s)"
                      << " overrun=" << drop << " gaps=" << gaps << '\n';
            last_rx = rx;
            last_print = now;
        }
    }

    nic.stop();
    nic.join();

    std::cout << "done rx=" << vi.stats().completed.load()
              << " overrun=" << vi.stats().rx_overrun.load() << " gaps=" << gaps
              << " lat_avg_us=" << hist.avg_us() << " p50_us=" << hist.percentile_us(0.50)
              << " p99_us=" << hist.percentile_us(0.99) << '\n';
    return 0;
#endif
}
