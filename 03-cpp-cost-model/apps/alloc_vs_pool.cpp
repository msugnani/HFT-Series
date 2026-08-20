#include "hft/common.hpp"
#include "hft/pool.hpp"

#include <cstdint>
#include <iostream>

namespace {

struct Node {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t c;
    std::uint64_t d;
};

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto iters = args.u64("--iters", 2000000);
    const auto pool_size = static_cast<std::size_t>(args.u64("--pool-size", 4096));

    std::uint64_t sink = 0;

    const std::uint64_t t_new0 = hft::now_ns();
    for (std::uint64_t i = 0; i < iters; ++i) {
        auto* n = new Node{i, i, i, i};
        sink ^= n->a;
        delete n;
    }
    const std::uint64_t t_new1 = hft::now_ns();

    hft::Pool<Node> pool(pool_size);
    const std::uint64_t t_pool0 = hft::now_ns();
    for (std::uint64_t i = 0; i < iters; ++i) {
        Node* n = pool.try_acquire();
        if (n == nullptr) {
            std::cerr << "pool exhausted — raise --pool-size or release in the loop\n";
            return 1;
        }
        n->a = i;
        sink ^= n->a;
        pool.release(n);
    }
    const std::uint64_t t_pool1 = hft::now_ns();

    hft::do_not_optimize(sink);
    const double n1 = static_cast<double>(t_new1 - t_new0) / static_cast<double>(iters);
    const double n2 = static_cast<double>(t_pool1 - t_pool0) / static_cast<double>(iters);
    std::cout << "iters=" << iters << " pool_size=" << pool_size << " sink=" << sink << '\n';
    std::cout << "new_delete ns_per=" << n1 << " elapsed_ns=" << (t_new1 - t_new0) << '\n';
    std::cout << "pool       ns_per=" << n2 << " elapsed_ns=" << (t_pool1 - t_pool0) << '\n';
    return 0;
}
