#include "hft/common.hpp"

#include <cstdint>
#include <iostream>

namespace {

struct Handler {
    virtual ~Handler() = default;
    virtual std::uint64_t on(std::uint64_t x) const = 0;
};

struct Add1 final : Handler {
    std::uint64_t on(std::uint64_t x) const override { return x + 1; }
};

struct Xor1 final : Handler {
    std::uint64_t on(std::uint64_t x) const override { return x ^ 1; }
};

std::uint64_t add1(std::uint64_t x) { return x + 1; }
std::uint64_t xor1(std::uint64_t x) { return x ^ 1; }

std::uint64_t tagged(std::uint64_t x, int tag) {
    switch (tag) {
        case 0:
            return x + 1;
        default:
            return x ^ 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 20000000);

    Add1 a;
    Xor1 b;
    Handler* handlers[2] = {&a, &b};
    using Fn = std::uint64_t (*)(std::uint64_t);
    Fn fns[2] = {&add1, &xor1};

    auto time_it = [&](auto&& fn, const char* name) {
        std::uint64_t sink = 0;
        // Warm the I-cache.
        for (std::uint64_t i = 0; i < 10000; ++i) {
            sink ^= fn(i);
        }
        const std::uint64_t t0 = hft::now_ns();
        for (std::uint64_t i = 0; i < iters; ++i) {
            sink ^= fn(i);
        }
        const std::uint64_t t1 = hft::now_ns();
        hft::do_not_optimize(sink);
        const double ns = static_cast<double>(t1 - t0) / static_cast<double>(iters);
        std::cout << name << " elapsed_ns=" << (t1 - t0) << " ns_per=" << ns << " sink=" << sink
                  << '\n';
    };

    time_it(
        [&](std::uint64_t i) { return handlers[i & 1]->on(i); }, "virtual");
    time_it(
        [&](std::uint64_t i) { return fns[i & 1](i); }, "fn_ptr");
    time_it(
        [&](std::uint64_t i) { return tagged(i, static_cast<int>(i & 1)); }, "switch_tag");

    return 0;
}
