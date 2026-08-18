#include "rxsim/simulated_nic.hpp"

#include "rxsim/packet.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <cerrno>
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

namespace rxsim {

SimulatedNic::SimulatedNic(VirtualInterface& vi) : vi_(vi) {}

SimulatedNic::~SimulatedNic() {
    stop();
    join();
}

bool SimulatedNic::dma_or_drop(const void* src, std::uint16_t length, bool wait_for_buffer) {
    RxDescriptor desc{};
    if (wait_for_buffer) {
        while (!vi_.try_acquire_rx(desc)) {
            std::this_thread::yield();
        }
    } else if (!vi_.try_acquire_rx(desc)) {
        // App has not posted (or not yet reposted) a buffer. A real NIC
        // drops here — a larger ring only delays this under sustained load.
        vi_.record_overrun();
        return false;
    }

    auto& pool = vi_.pool();
    const std::size_t n = std::min<std::size_t>(length, pool.buffer_size());
    // Fake DMA: CPU memcpy into a posted buffer. On ef_vi the NIC writes
    // this address without the CPU.
    std::memcpy(pool.data(desc.buffer_id), src, n);
    vi_.complete_rx(desc.buffer_id, static_cast<std::uint16_t>(n));
    return true;
}

void SimulatedNic::run_fake(std::uint64_t packet_count, std::uint16_t payload_size,
                            bool yield_each, bool wait_for_buffer) {
    if (payload_size == 0) {
        payload_size = kMinPayload;
    }
    std::vector<std::uint8_t> pkt(payload_size, 0);
    for (std::uint64_t seq = 0; seq < packet_count; ++seq) {
        if (payload_size >= sizeof(std::uint64_t)) {
            std::memcpy(pkt.data(), &seq, sizeof(seq));
        }
        dma_or_drop(pkt.data(), payload_size, wait_for_buffer);
        if (yield_each) {
            std::this_thread::yield();
        }
    }
}

#ifdef __linux__

void SimulatedNic::start_udp(std::uint16_t port, int batch) {
    if (running_.load(std::memory_order_acquire)) {
        throw std::runtime_error("SimulatedNic already running");
    }
    if (batch < 1) {
        batch = 1;
    }
    batch_ = batch;

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error("socket() failed");
    }

    int reuse = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Keep the kernel socket buffer large so drops we observe are *our*
    // descriptor-ring overruns, not UDP socket overflow.
    int rcvbuf = 4 * 1024 * 1024;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("bind() failed");
    }

    const int flags = ::fcntl(fd_, F_GETFL, 0);
    ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { udp_loop(); });
}

void SimulatedNic::udp_loop() {
    const int batch = batch_;
    const std::size_t bufsz = vi_.pool().buffer_size();

    std::vector<std::vector<std::uint8_t>> staging(static_cast<std::size_t>(batch),
                                                   std::vector<std::uint8_t>(bufsz));
    std::vector<iovec> iov(static_cast<std::size_t>(batch));
    std::vector<mmsghdr> msgs(static_cast<std::size_t>(batch));
    std::vector<sockaddr_in> addrs(static_cast<std::size_t>(batch));

    for (int i = 0; i < batch; ++i) {
        iov[static_cast<std::size_t>(i)].iov_base = staging[static_cast<std::size_t>(i)].data();
        iov[static_cast<std::size_t>(i)].iov_len = bufsz;
        std::memset(&msgs[static_cast<std::size_t>(i)], 0, sizeof(mmsghdr));
        msgs[static_cast<std::size_t>(i)].msg_hdr.msg_iov = &iov[static_cast<std::size_t>(i)];
        msgs[static_cast<std::size_t>(i)].msg_hdr.msg_iovlen = 1;
        msgs[static_cast<std::size_t>(i)].msg_hdr.msg_name = &addrs[static_cast<std::size_t>(i)];
        msgs[static_cast<std::size_t>(i)].msg_hdr.msg_namelen = sizeof(sockaddr_in);
    }

    while (running_.load(std::memory_order_acquire)) {
        const int n = ::recvmmsg(fd_, msgs.data(), static_cast<unsigned int>(batch), MSG_DONTWAIT,
                                 nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                RXSIM_PAUSE();
                continue;
            }
            break;
        }
        for (int i = 0; i < n; ++i) {
            const auto len = static_cast<std::uint16_t>(
                std::min<std::size_t>(msgs[static_cast<std::size_t>(i)].msg_len, bufsz));
            dma_or_drop(staging[static_cast<std::size_t>(i)].data(), len);
        }
    }
}

void SimulatedNic::stop() {
    running_.store(false, std::memory_order_release);
}

void SimulatedNic::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

#else

void SimulatedNic::start_udp(std::uint16_t, int) {
    throw std::runtime_error("UDP NIC requires Linux/WSL (recvmmsg)");
}

void SimulatedNic::udp_loop() {}

void SimulatedNic::stop() {
    running_.store(false, std::memory_order_release);
}

void SimulatedNic::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

#endif

}  // namespace rxsim
