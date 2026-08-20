#include "hft/common.hpp"
#include "lob/book.hpp"
#include "lob/stl_book.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

struct Stats {
    std::uint64_t ops{0};
    std::uint64_t fills{0};
    std::uint64_t fill_qty{0};
    std::uint64_t cancels{0};
    std::uint64_t checksum{0};
    std::uint32_t best_bid{0};
    std::uint32_t best_ask{0};
    std::uint32_t live{0};
};

template <typename B>
Stats run(B& book,
          std::uint64_t n,
          std::uint32_t px_lo,
          std::uint32_t px_hi,
          int cancel_pct,
          std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<std::uint32_t> px_dist(px_lo, px_hi);
    std::uniform_int_distribution<std::uint32_t> qty_dist(1, 8);
    std::uniform_int_distribution<int> pct_dist(0, 99);

    std::vector<std::uint32_t> live;
    live.reserve(4096);
    lob::Exec execs[32];
    Stats st;

    for (std::uint64_t i = 0; i < n; ++i) {
        if (!live.empty() && pct_dist(rng) < cancel_pct) {
            const std::uint32_t id = live.back();
            live.pop_back();
            if (book.cancel(id)) {
                ++st.cancels;
            }
            ++st.ops;
            continue;
        }

        const auto side = side_dist(rng) == 0 ? lob::Side::Buy : lob::Side::Sell;
        const std::uint32_t px = px_dist(rng);
        const std::uint32_t qty = qty_dist(rng);
        std::uint32_t nexec = 0;
        const std::uint32_t id = book.limit(side, px, qty, execs, 32, nexec);
        st.fills += nexec;
        for (std::uint32_t e = 0; e < nexec && e < 32; ++e) {
            st.fill_qty += execs[e].qty;
                st.checksum += execs[e].px + static_cast<std::uint64_t>(execs[e].qty) * 1000003ull;
        }
        if (id != lob::kInvalidId) {
            live.push_back(id);
        }
        ++st.ops;
    }

    st.best_bid = book.best_bid();
    st.best_ask = book.best_ask();
    st.live = book.live_orders();
    st.checksum += st.best_bid + st.best_ask + st.live + st.fills + st.fill_qty + st.cancels;
    return st;
}

void print_stats(const char* name, std::uint64_t elapsed_ns, Stats const& st) {
    const double ns_per = static_cast<double>(elapsed_ns) / static_cast<double>(st.ops);
    std::cout << name << " ops=" << st.ops << " elapsed_ns=" << elapsed_ns << " ns_per=" << ns_per
              << " fills=" << st.fills << " fill_qty=" << st.fill_qty << " cancels=" << st.cancels
              << " live=" << st.live << " best=" << st.best_bid << "/" << st.best_ask
              << " checksum=" << st.checksum << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto n = args.u64("--n", 500000);
    const auto px_lo = static_cast<std::uint32_t>(args.u64("--px-lo", 90));
    const auto px_hi = static_cast<std::uint32_t>(args.u64("--px-hi", 110));
    const auto max_orders = static_cast<std::uint32_t>(args.u64("--max-orders", 65536));
    const int cancel_pct = args.i32("--cancel-pct", 25);
    const auto seed = static_cast<std::uint32_t>(args.u64("--seed", 1));
    const int stl = args.i32("--stl", 1);

    std::cout << "n=" << n << " px=" << px_lo << ".." << px_hi << " max_orders=" << max_orders
              << " cancel_pct=" << cancel_pct << " seed=" << seed << '\n';

    lob::Book ladder(px_lo, px_hi, max_orders);
    const std::uint64_t t0 = hft::now_ns();
    const Stats a = run(ladder, n, px_lo, px_hi, cancel_pct, seed);
    const std::uint64_t t1 = hft::now_ns();
    hft::do_not_optimize(a.checksum);
    print_stats("ladder", t1 - t0, a);

    if (stl) {
        lob::StlBook tree(px_lo, px_hi, max_orders);
        const std::uint64_t t2 = hft::now_ns();
        const Stats b = run(tree, n, px_lo, px_hi, cancel_pct, seed);
        const std::uint64_t t3 = hft::now_ns();
        hft::do_not_optimize(b.checksum);
        print_stats("stl   ", t3 - t2, b);
        if (a.checksum != b.checksum || a.fills != b.fills || a.best_bid != b.best_bid ||
            a.best_ask != b.best_ask || a.live != b.live) {
            std::cerr << "ladder and stl diverged — matching rules should be identical\n";
            return 1;
        }
    }
    return 0;
}
