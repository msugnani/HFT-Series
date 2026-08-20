#pragma once

#include "lob/types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lob {

// Price-time (FIFO) limit book on a dense tick ladder.
//
// Why an array of levels, not std::map
// ------------------------------------
// Tick is an integer. If the instrument's range is bounded, level lookup is
// `levels[px]` — one load — the same lesson as hashmap_vs_array in chapter 03.
// A map gives you sparse prices for free and costs a branchy tree walk plus
// allocations on every new level (see StlBook).
//
// Why intrusive FIFO, not deque
// -----------------------------
// Cancel must be O(1). The order id is a pool index, so we can unlink the node
// without scanning the level. deque::erase is O(depth).
//
// Why one thread
// --------------
// Matching is a serialisation point: two cores matching the same instrument is
// a correctness bug, not a speedup. Scale by sharding instruments, not by
// locking the ladder.
//
// Emptying the best level walks toward the other side over the tick range.
// That is the trade we made for O(1) qty_at(px). A sparse occupied-level list
// makes "next best" O(1) at the cost of more pointer chasing on insert.
class Book {
public:
    Book(std::uint32_t min_px, std::uint32_t max_px, std::uint32_t max_orders)
        : min_px_(min_px),
          max_px_(max_px),
          max_orders_(max_orders),
          bids_(static_cast<std::size_t>(max_px) + 1),
          asks_(static_cast<std::size_t>(max_px) + 1),
          orders_(static_cast<std::size_t>(max_orders) + 1),
          free_(max_orders) {
        if (min_px < 1 || max_px < min_px || max_orders < 1) {
            throw std::invalid_argument("Book: need min_px >= 1, max_px >= min_px, max_orders >= 1");
        }
        for (std::uint32_t i = 0; i < max_orders; ++i) {
            free_[i] = max_orders - i;  // first acquire returns 1
        }
        free_top_ = max_orders;
    }

    [[nodiscard]] std::uint32_t min_px() const noexcept { return min_px_; }
    [[nodiscard]] std::uint32_t max_px() const noexcept { return max_px_; }
    [[nodiscard]] std::uint32_t max_orders() const noexcept { return max_orders_; }
    [[nodiscard]] std::uint32_t live_orders() const noexcept { return max_orders_ - free_top_; }
    [[nodiscard]] std::uint32_t best_bid() const noexcept { return best_bid_; }
    [[nodiscard]] std::uint32_t best_ask() const noexcept { return best_ask_; }

    [[nodiscard]] std::uint32_t qty_at(Side side, std::uint32_t px) const noexcept {
        if (px < min_px_ || px > max_px_) {
            return 0;
        }
        return side == Side::Buy ? bids_[px].qty : asks_[px].qty;
    }

    // Aggressive remainder matches; leftover rests at px. Returns resting id,
    // or kInvalidId if fully filled, rejected, or the pool is empty.
    // execs may be null. Matching always runs to completion; extra execs past
    // exec_cap are counted in nexec but not stored.
    std::uint32_t limit(Side side,
                        std::uint32_t px,
                        std::uint32_t qty,
                        Exec* execs,
                        std::uint32_t exec_cap,
                        std::uint32_t& nexec) noexcept {
        nexec = 0;
        if (qty == 0 || px < min_px_ || px > max_px_) {
            return kInvalidId;
        }
        const std::uint32_t id = acquire();
        if (id == kInvalidId) {
            return kInvalidId;
        }
        Order& o = orders_[id];
        o.px = px;
        o.qty = qty;
        o.side = side;
        o.prev = 0;
        o.next = 0;

        o.qty = match(id, execs, exec_cap, nexec);
        if (o.qty == 0) {
            release(id);
            return kInvalidId;
        }
        rest(id);
        return id;
    }

    bool cancel(std::uint32_t id) noexcept {
        if (id == kInvalidId || id > max_orders_) {
            return false;
        }
        Order& o = orders_[id];
        if (o.qty == 0) {
            return false;  // free slot or not resting
        }
        Level& lv = level(o.side, o.px);
        lv.qty -= o.qty;
        unlink(id);
        const Side side = o.side;
        const std::uint32_t px = o.px;
        release(id);
        if (lv.qty == 0) {
            on_level_empty(side, px);
        }
        return true;
    }

private:
    struct Order {
        std::uint32_t prev{0};
        std::uint32_t next{0};
        std::uint32_t qty{0};
        std::uint32_t px{0};
        Side side{Side::Buy};
    };

    struct Level {
        std::uint32_t head{0};
        std::uint32_t tail{0};
        std::uint32_t qty{0};
    };

    [[nodiscard]] Level& level(Side side, std::uint32_t px) noexcept {
        return side == Side::Buy ? bids_[px] : asks_[px];
    }

    std::uint32_t acquire() noexcept {
        if (free_top_ == 0) {
            return kInvalidId;
        }
        return free_[--free_top_];
    }

    void release(std::uint32_t id) noexcept {
        orders_[id] = Order{};
        free_[free_top_++] = id;
    }

    void unlink(std::uint32_t id) noexcept {
        Order& o = orders_[id];
        Level& lv = level(o.side, o.px);
        if (o.prev != 0) {
            orders_[o.prev].next = o.next;
        } else {
            lv.head = o.next;
        }
        if (o.next != 0) {
            orders_[o.next].prev = o.prev;
        } else {
            lv.tail = o.prev;
        }
        o.prev = 0;
        o.next = 0;
    }

    void rest(std::uint32_t id) noexcept {
        Order& o = orders_[id];
        Level& lv = level(o.side, o.px);
        o.prev = lv.tail;
        o.next = 0;
        if (lv.tail != 0) {
            orders_[lv.tail].next = id;
        } else {
            lv.head = id;
        }
        lv.tail = id;
        lv.qty += o.qty;
        if (o.side == Side::Buy) {
            if (best_bid_ == 0 || o.px > best_bid_) {
                best_bid_ = o.px;
            }
        } else if (best_ask_ == 0 || o.px < best_ask_) {
            best_ask_ = o.px;
        }
    }

    std::uint32_t match(std::uint32_t taker_id,
                        Exec* execs,
                        std::uint32_t exec_cap,
                        std::uint32_t& nexec) noexcept {
        Order& taker = orders_[taker_id];
        std::uint32_t remaining = taker.qty;
        if (taker.side == Side::Buy) {
            while (remaining != 0 && best_ask_ != 0 && best_ask_ <= taker.px) {
                remaining = take_level(asks_[best_ask_], taker_id, remaining, execs, exec_cap, nexec);
                if (asks_[best_ask_].qty == 0) {
                    tighten_ask();
                }
            }
        } else {
            while (remaining != 0 && best_bid_ != 0 && best_bid_ >= taker.px) {
                remaining = take_level(bids_[best_bid_], taker_id, remaining, execs, exec_cap, nexec);
                if (bids_[best_bid_].qty == 0) {
                    tighten_bid();
                }
            }
        }
        return remaining;
    }

    std::uint32_t take_level(Level& lv,
                            std::uint32_t taker_id,
                            std::uint32_t remaining,
                            Exec* execs,
                            std::uint32_t exec_cap,
                            std::uint32_t& nexec) noexcept {
        while (remaining != 0 && lv.head != 0) {
            const std::uint32_t maker_id = lv.head;
            Order& maker = orders_[maker_id];
            const std::uint32_t take = std::min(maker.qty, remaining);
            if (execs != nullptr && nexec < exec_cap) {
                execs[nexec] = Exec{maker_id, taker_id, maker.px, take};
            }
            ++nexec;
            maker.qty -= take;
            lv.qty -= take;
            remaining -= take;
            if (maker.qty == 0) {
                unlink(maker_id);
                release(maker_id);
            }
        }
        return remaining;
    }

    void on_level_empty(Side side, std::uint32_t px) noexcept {
        if (side == Side::Buy && px == best_bid_) {
            tighten_bid();
        } else if (side == Side::Sell && px == best_ask_) {
            tighten_ask();
        }
    }

    void tighten_bid() noexcept {
        std::uint32_t p = best_bid_;
        while (p >= min_px_ && bids_[p].qty == 0) {
            if (p == min_px_) {
                best_bid_ = 0;
                return;
            }
            --p;
        }
        best_bid_ = p;
    }

    void tighten_ask() noexcept {
        std::uint32_t p = best_ask_;
        while (p <= max_px_ && asks_[p].qty == 0) {
            if (p == max_px_) {
                best_ask_ = 0;
                return;
            }
            ++p;
        }
        best_ask_ = p;
    }

    std::uint32_t min_px_;
    std::uint32_t max_px_;
    std::uint32_t max_orders_;
    std::uint32_t best_bid_{0};
    std::uint32_t best_ask_{0};
    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::vector<Order> orders_;
    std::vector<std::uint32_t> free_;
    std::uint32_t free_top_{0};
};

}  // namespace lob
