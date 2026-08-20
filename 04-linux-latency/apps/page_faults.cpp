#include "hft/common.hpp"

#include <cstdint>
#include <iostream>
#include <new>
#include <vector>

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace {

constexpr std::size_t kPage = 4096;

std::uint64_t touch(char* p, std::size_t bytes, char value) {
    std::uint64_t sink = 0;
    for (std::size_t i = 0; i < bytes; i += kPage) {
        p[i] = value;
        sink += static_cast<unsigned char>(p[i]);
    }
    return sink;
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto bytes = static_cast<std::size_t>(args.u64("--bytes", 64ull << 20));
    const int use_mlock = args.i32("--mlock", 0);
    const int huge = args.i32("--huge", 0);

    char* buf = nullptr;
    bool mapped = false;

#ifdef __linux__
    if (huge) {
        buf = static_cast<char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
        if (buf == MAP_FAILED) {
            std::cerr << "MAP_HUGETLB failed (no huge-page pool?). Falling back to anonymous.\n";
            buf = nullptr;
        } else {
            mapped = true;
        }
    }
    if (buf == nullptr) {
        buf = static_cast<char*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (buf == MAP_FAILED) {
            std::cerr << "mmap failed\n";
            return 1;
        }
        mapped = true;
    }
    if (use_mlock) {
        if (mlock(buf, bytes) != 0) {
            std::cerr << "mlock failed (ulimit -l?). Continuing unlocked.\n";
        }
    }
#else
    (void)use_mlock;
    (void)huge;
    buf = new (std::nothrow) char[bytes];
    if (buf == nullptr) {
        std::cerr << "allocation failed\n";
        return 1;
    }
#endif

    const std::uint64_t t0 = hft::now_ns();
    const std::uint64_t s0 = touch(buf, bytes, 1);
    const std::uint64_t t1 = hft::now_ns();
    const std::uint64_t s1 = touch(buf, bytes, 2);
    const std::uint64_t t2 = hft::now_ns();

    hft::do_not_optimize(s0);
    hft::do_not_optimize(s1);

    const std::size_t pages = bytes / kPage;
    std::cout << "bytes=" << bytes << " pages=" << pages << " mlock=" << use_mlock
              << " huge=" << huge << '\n';
    std::cout << "first_touch_ns=" << (t1 - t0) << " ns_per_page="
              << (static_cast<double>(t1 - t0) / static_cast<double>(pages)) << '\n';
    std::cout << "second_pass_ns=" << (t2 - t1) << " ns_per_page="
              << (static_cast<double>(t2 - t1) / static_cast<double>(pages)) << '\n';

#ifdef __linux__
    if (mapped) {
        munmap(buf, bytes);
    }
#else
    delete[] buf;
#endif
    (void)mapped;
    return 0;
}
