#include "common.hpp"

#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__

int main(int argc, char** argv) {
    const Argv args{argc, argv};
    if (args.has("--help")) {
        std::cout
            << "sender --host 127.0.0.1 --port 9000 --count 100000 --payload 64\n"
            << "       [--rate 200000]          constant packets/sec (0 = as fast as possible)\n"
            << "       [--burst 10000 --idle-ms 50]  blast `burst` packets, then idle, repeat\n";
        return 0;
    }

    const std::string host = args.get("--host", "127.0.0.1");
    const std::uint16_t port = static_cast<std::uint16_t>(args.u64("--port", 9000));
    const std::uint64_t count = args.u64("--count", 100000);
    const std::uint64_t rate = args.u64("--rate", 0);
    const std::uint64_t burst = args.u64("--burst", 0);
    const std::uint64_t idle_ms = args.u64("--idle-ms", 50);
    std::uint16_t payload = static_cast<std::uint16_t>(args.u64("--payload", 64));
    if (payload < rxsim::kMinPayload) {
        payload = rxsim::kMinPayload;
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "socket() failed\n";
        return 1;
    }
    int sndbuf = 4 * 1024 * 1024;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid --host\n";
        ::close(fd);
        return 1;
    }

    std::vector<std::uint8_t> pkt(payload, 0);
    const auto interval_ns = rate == 0 ? 0 : 1000000000ull / rate;
    std::uint64_t next_ns = now_ns();
    std::uint64_t in_burst = 0;

    const auto t0 = now_ns();
    for (std::uint64_t seq = 0; seq < count; ++seq) {
        rxsim::PacketHeader hdr{seq, now_ns()};
        std::memcpy(pkt.data(), &hdr, sizeof(hdr));

        const ssize_t n = ::sendto(fd, pkt.data(), payload, 0,
                                   reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (n < 0) {
            std::cerr << "sendto failed at seq=" << seq << '\n';
            break;
        }

        if (burst > 0) {
            ++in_burst;
            if (in_burst >= burst) {
                in_burst = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(idle_ms));
            }
        } else if (interval_ns > 0) {
            next_ns += interval_ns;
            const std::uint64_t now = now_ns();
            if (now < next_ns) {
                const auto sleep_ns = next_ns - now;
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
        }
    }
    const auto elapsed_ns = now_ns() - t0;
    const double secs = static_cast<double>(elapsed_ns) / 1e9;
    std::cout << "sent " << count << " packets in " << secs << " s ("
              << (secs > 0 ? static_cast<double>(count) / secs : 0.0) << " pps)\n";
    ::close(fd);
    return 0;
}

#else

int main() {
    std::cerr << "sender requires Linux/WSL\n";
    return 1;
}

#endif
