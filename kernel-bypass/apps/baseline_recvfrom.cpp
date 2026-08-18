#include "common.hpp"

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define RXSIM_PAUSE() _mm_pause()
#else
#define RXSIM_PAUSE() ((void)0)
#endif
#endif

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef __linux__

int main(int argc, char** argv) {
    const Argv args{argc, argv};
    if (args.has("--help")) {
        std::cout
            << "baseline_recvfrom --port 9000 [--batch 1] [--cpu 2] [--seconds 0]\n"
            << "  --batch 1 uses recvfrom; --batch N>1 uses recvmmsg\n";
        return 0;
    }

    const std::uint16_t port = static_cast<std::uint16_t>(args.u64("--port", 9000));
    const int batch = args.i32("--batch", 1);
    const int cpu = args.i32("--cpu", -1);
    const std::uint64_t seconds = args.u64("--seconds", 0);
    const std::size_t bufsz = static_cast<std::size_t>(args.u64("--payload", 2048));

    if (!pin_cpu(cpu)) {
        std::cerr << "warning: failed to pin CPU " << cpu << '\n';
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "socket() failed\n";
        return 1;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int rcvbuf = 4 * 1024 * 1024;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind() failed\n";
        ::close(fd);
        return 1;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    std::cout << "baseline listening on UDP :" << port << " batch=" << batch << '\n';

    LatencyHist hist;
    std::uint64_t expected = 0;
    std::uint64_t rx = 0;
    std::uint64_t gaps = 0;
    std::uint64_t last_print = now_ns();
    std::uint64_t last_rx = 0;
    const std::uint64_t t0 = now_ns();
    const std::uint64_t deadline = seconds == 0 ? 0 : t0 + seconds * 1000000000ull;

    auto handle = [&](const std::uint8_t* raw, std::size_t len) {
        ++rx;
        if (len < rxsim::kMinPayload) {
            return;
        }
        rxsim::PacketHeader hdr{};
        std::memcpy(&hdr, raw, sizeof(hdr));
        if (hdr.seq > expected) {
            gaps += hdr.seq - expected;
        }
        expected = hdr.seq + 1;
        if (hdr.timestamp_ns != 0 && hdr.timestamp_ns <= now_ns()) {
            hist.add_us((now_ns() - hdr.timestamp_ns) / 1000);
        }
    };

    if (batch <= 1) {
        std::vector<std::uint8_t> buf(bufsz);
        while (deadline == 0 || now_ns() < deadline) {
            const ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), MSG_DONTWAIT, nullptr, nullptr);
            if (n > 0) {
                handle(buf.data(), static_cast<std::size_t>(n));
            } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                RXSIM_PAUSE();
            } else {
                break;
            }
            const std::uint64_t now = now_ns();
            if (now - last_print >= 1000000000ull) {
                std::cout << "rx=" << rx << " (+" << (rx - last_rx) << "/s) gaps=" << gaps << '\n';
                last_rx = rx;
                last_print = now;
            }
        }
    } else {
        std::vector<std::vector<std::uint8_t>> staging(static_cast<std::size_t>(batch),
                                                       std::vector<std::uint8_t>(bufsz));
        std::vector<iovec> iov(static_cast<std::size_t>(batch));
        std::vector<mmsghdr> msgs(static_cast<std::size_t>(batch));
        for (int i = 0; i < batch; ++i) {
            iov[static_cast<std::size_t>(i)].iov_base = staging[static_cast<std::size_t>(i)].data();
            iov[static_cast<std::size_t>(i)].iov_len = bufsz;
            std::memset(&msgs[static_cast<std::size_t>(i)], 0, sizeof(mmsghdr));
            msgs[static_cast<std::size_t>(i)].msg_hdr.msg_iov = &iov[static_cast<std::size_t>(i)];
            msgs[static_cast<std::size_t>(i)].msg_hdr.msg_iovlen = 1;
        }
        while (deadline == 0 || now_ns() < deadline) {
            const int n = ::recvmmsg(fd, msgs.data(), static_cast<unsigned int>(batch), MSG_DONTWAIT,
                                     nullptr);
            if (n > 0) {
                for (int i = 0; i < n; ++i) {
                    handle(staging[static_cast<std::size_t>(i)].data(),
                           msgs[static_cast<std::size_t>(i)].msg_len);
                }
            } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                RXSIM_PAUSE();
            } else {
                break;
            }
            const std::uint64_t now = now_ns();
            if (now - last_print >= 1000000000ull) {
                std::cout << "rx=" << rx << " (+" << (rx - last_rx) << "/s) gaps=" << gaps << '\n';
                last_rx = rx;
                last_print = now;
            }
        }
    }

    std::cout << "done rx=" << rx << " gaps=" << gaps << " lat_avg_us=" << hist.avg_us()
              << " p50_us=" << hist.percentile_us(0.50) << " p99_us=" << hist.percentile_us(0.99)
              << '\n';
    ::close(fd);
    return 0;
}

#else

int main() {
    std::cerr << "baseline_recvfrom requires Linux/WSL\n";
    return 1;
}

#endif
