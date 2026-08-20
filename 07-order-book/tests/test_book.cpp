#include "lob/book.hpp"
#include "lob/stl_book.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

lob::Exec execs[16];

std::uint32_t buy(lob::Book& b, std::uint32_t px, std::uint32_t qty, std::uint32_t& n) {
    return b.limit(lob::Side::Buy, px, qty, execs, 16, n);
}

std::uint32_t sell(lob::Book& b, std::uint32_t px, std::uint32_t qty, std::uint32_t& n) {
    return b.limit(lob::Side::Sell, px, qty, execs, 16, n);
}

void test_no_cross() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    const auto bid = buy(b, 100, 10, n);
    const auto ask = sell(b, 101, 10, n);
    require(bid != 0 && ask != 0, "both rest");
    require(n == 0, "no trade");
    require(b.best_bid() == 100 && b.best_ask() == 101, "spread 100/101");
    require(b.qty_at(lob::Side::Buy, 100) == 10, "bid qty");
    require(b.qty_at(lob::Side::Sell, 101) == 10, "ask qty");
}

void test_fifo_same_level() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    const auto a = sell(b, 100, 5, n);
    const auto c = sell(b, 100, 5, n);
    require(a != 0 && c != 0 && n == 0, "two resting sells");
    n = 0;
    const auto t = buy(b, 100, 5, n);
    require(t == 0, "taker fully filled");
    require(n == 1, "one fill");
    require(execs[0].maker_id == a, "oldest maker first");
    require(execs[0].qty == 5 && execs[0].px == 100, "fill 5 @ 100");
    require(b.qty_at(lob::Side::Sell, 100) == 5, "second order still live");
    require(b.cancel(c), "cancel remainder");
    require(b.best_ask() == 0, "ask empty");
}

void test_partial_then_rest() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    require(sell(b, 100, 3, n) != 0, "resting ask 3");
    n = 0;
    const auto id = buy(b, 100, 10, n);
    require(n == 1 && execs[0].qty == 3, "took the ask");
    require(id != 0, "remainder rests");
    require(b.best_bid() == 100 && b.best_ask() == 0, "now a bid");
    require(b.qty_at(lob::Side::Buy, 100) == 7, "7 left");
}

void test_walk_levels() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    require(sell(b, 100, 1, n) != 0, "ask 100");
    require(sell(b, 101, 1, n) != 0, "ask 101");
    require(sell(b, 102, 1, n) != 0, "ask 102");
    n = 0;
    const auto id = buy(b, 101, 2, n);
    require(id == 0, "fully filled");
    require(n == 2, "only 100 and 101 are in range");
    require(execs[0].px == 100 && execs[1].px == 101, "maker prices, best first");
    require(b.best_ask() == 102, "102 untouched");
    require(b.qty_at(lob::Side::Sell, 102) == 1, "one lot left");
}

void test_cancel_best_tightens() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    const auto top = buy(b, 110, 1, n);
    require(buy(b, 100, 1, n) != 0, "worse bid");
    require(b.best_bid() == 110, "best is 110");
    require(b.cancel(top), "cancel best");
    require(b.best_bid() == 100, "tightens to 100");
}

void test_cancel_then_no_match() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    const auto id = buy(b, 100, 5, n);
    require(b.cancel(id), "cancel");
    require(!b.cancel(id), "double cancel fails");
    n = 0;
    require(sell(b, 100, 5, n) != 0, "sell rests");
    require(n == 0, "nothing to hit");
}

void test_pool_exhaustion() {
    lob::Book b(1, 200, 2);
    std::uint32_t n = 0;
    require(buy(b, 100, 1, n) != 0, "order 1");
    require(buy(b, 99, 1, n) != 0, "order 2");
    require(buy(b, 98, 1, n) == 0, "pool empty");
    require(b.live_orders() == 2, "still two live");
}

void test_trade_at_maker_px() {
    lob::Book b(1, 200, 32);
    std::uint32_t n = 0;
    require(sell(b, 100, 1, n) != 0, "ask 100");
    n = 0;
    require(buy(b, 105, 1, n) == 0, "marketable buy");
    require(n == 1 && execs[0].px == 100, "executes at resting 100, not 105");
}

void test_ladder_matches_stl() {
    lob::Book ladder(1, 30, 64);
    lob::StlBook stl(1, 30, 64);
    struct Op {
        lob::Side side;
        std::uint32_t px;
        std::uint32_t qty;
        bool cancel_last;
    };
    const Op tape[] = {
        {lob::Side::Buy, 10, 5, false},
        {lob::Side::Buy, 10, 5, false},
        {lob::Side::Sell, 12, 3, false},
        {lob::Side::Sell, 10, 7, false},
        {lob::Side::Buy, 12, 3, false},
        {lob::Side::Buy, 9, 4, false},
        {lob::Side::Sell, 9, 1, false},
        {lob::Side::Buy, 0, 0, true},
    };
    std::vector<std::uint32_t> live_l;
    std::vector<std::uint32_t> live_s;
    lob::Exec el[8];
    lob::Exec es[8];
    for (auto const& op : tape) {
        if (op.cancel_last) {
            require(!live_l.empty() && !live_s.empty(), "cancel with live ids");
            require(ladder.cancel(live_l.back()) == stl.cancel(live_s.back()), "cancel both");
            live_l.pop_back();
            live_s.pop_back();
            continue;
        }
        std::uint32_t nl = 0;
        std::uint32_t ns = 0;
        const auto il = ladder.limit(op.side, op.px, op.qty, el, 8, nl);
        const auto is = stl.limit(op.side, op.px, op.qty, es, 8, ns);
        require(nl == ns, "same fill count");
        for (std::uint32_t i = 0; i < nl; ++i) {
            require(el[i].px == es[i].px && el[i].qty == es[i].qty, "same fill px/qty");
        }
        const bool same_rest = (il == 0) == (is == 0);
        require(same_rest, "both rest or both fully fill");
        if (il != 0) {
            live_l.push_back(il);
        }
        if (is != 0) {
            live_s.push_back(is);
        }
    }
    require(ladder.best_bid() == stl.best_bid(), "best bid");
    require(ladder.best_ask() == stl.best_ask(), "best ask");
    require(ladder.live_orders() == stl.live_orders(), "live");
}

}  // namespace

int main() {
    try {
        test_no_cross();
        test_fifo_same_level();
        test_partial_then_rest();
        test_walk_levels();
        test_cancel_best_tightens();
        test_cancel_then_no_match();
        test_pool_exhaustion();
        test_trade_at_maker_px();
        test_ladder_matches_stl();
    } catch (const std::exception& ex) {
        std::cerr << "test_book FAILED: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "test_book OK\n";
    return 0;
}
