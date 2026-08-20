#include "lob/book.hpp"

#include <cstdint>
#include <iostream>

namespace {

const char* side_str(lob::Side s) { return s == lob::Side::Buy ? "BID" : "ASK"; }

void print_side(lob::Book const& b, lob::Side side, std::uint32_t start, int dir, int max_levels) {
    std::uint32_t px = start;
    int shown = 0;
    while (px >= b.min_px() && px <= b.max_px() && shown < max_levels) {
        const std::uint32_t q = b.qty_at(side, px);
        if (q != 0) {
            std::cout << "  " << px << " x " << q << '\n';
            ++shown;
        }
        if (dir < 0) {
            if (px == b.min_px()) {
                break;
            }
            --px;
        } else {
            if (px == b.max_px()) {
                break;
            }
            ++px;
        }
    }
    if (shown == 0) {
        std::cout << "  (empty)\n";
    }
}

void print_book(lob::Book const& b) {
    std::cout << "ASK\n";
    print_side(b, lob::Side::Sell, b.best_ask() == 0 ? b.min_px() : b.best_ask(), +1, 8);
    std::cout << "---- best " << b.best_bid() << " / " << b.best_ask() << " ----\n";
    std::cout << "BID\n";
    print_side(b, lob::Side::Buy, b.best_bid() == 0 ? b.max_px() : b.best_bid(), -1, 8);
    std::cout << "live=" << b.live_orders() << "\n\n";
}

std::uint32_t submit(lob::Book& b, lob::Side side, std::uint32_t px, std::uint32_t qty) {
    lob::Exec execs[8];
    std::uint32_t n = 0;
    const std::uint32_t id = b.limit(side, px, qty, execs, 8, n);
    std::cout << side_str(side) << " " << qty << " @ " << px << " -> id=" << id << " fills=" << n;
    for (std::uint32_t i = 0; i < n && i < 8; ++i) {
        std::cout << "  " << execs[i].qty << "@" << execs[i].px << "(maker " << execs[i].maker_id
                  << ")";
    }
    std::cout << '\n';
    return id;
}

}  // namespace

int main() {
    lob::Book b(90, 110, 64);

    std::cout << "Two bids at 100 (FIFO) and a worse bid at 99:\n";
    submit(b, lob::Side::Buy, 100, 10);
    submit(b, lob::Side::Buy, 100, 5);
    submit(b, lob::Side::Buy, 99, 2);
    print_book(b);

    std::cout << "Ask that does not cross:\n";
    submit(b, lob::Side::Sell, 102, 4);
    print_book(b);

    std::cout << "Sell 12 @ 100 hits the FIFO at 100 (10, then 2 of 5). Fully filled.\n";
    submit(b, lob::Side::Sell, 100, 12);
    print_book(b);

    std::cout << "Buy 4 @ 102 lifts the ask at the maker price (102):\n";
    submit(b, lob::Side::Buy, 102, 4);
    print_book(b);

    return 0;
}
