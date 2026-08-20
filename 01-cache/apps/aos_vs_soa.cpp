#include "hft/common.hpp"

#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

// Hot loop reads only px. AoS still pulls the rest of the struct into cache.

namespace {

struct Order {
    std::uint64_t id;
    double px;
    double qty;
    std::uint64_t ts_ns;
};

double sum_aos(std::vector<Order> const& orders) {
    double s = 0;
    for (auto const& o : orders) {
        s += o.px;
    }
    return s;
}

double sum_soa(std::vector<double> const& px) {
    double s = 0;
    for (double p : px) {
        s += p;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto n = static_cast<std::size_t>(args.u64("--n", 4000000));

    std::vector<Order> aos(n);
    std::vector<double> soa(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double px = static_cast<double>(i & 1023);
        aos[i] = Order{i, px, 1.0, 0};
        soa[i] = px;
    }

    // Touch once so page faults are not in the timed region.
    hft::do_not_optimize(sum_aos(aos));
    hft::do_not_optimize(sum_soa(soa));

    const std::uint64_t t0 = hft::now_ns();
    const double a = sum_aos(aos);
    const std::uint64_t t1 = hft::now_ns();
    const double b = sum_soa(soa);
    const std::uint64_t t2 = hft::now_ns();

    hft::do_not_optimize(a);
    hft::do_not_optimize(b);

    std::cout << "n=" << n << " sizeof(Order)=" << sizeof(Order) << '\n';
    std::cout << "aos_ns=" << (t1 - t0) << " soa_ns=" << (t2 - t1) << " checksum=" << (a + b)
              << '\n';
    std::cout << "bytes_touched_aos~=" << n * sizeof(Order)
              << " bytes_touched_soa~=" << n * sizeof(double) << '\n';
    return 0;
}
