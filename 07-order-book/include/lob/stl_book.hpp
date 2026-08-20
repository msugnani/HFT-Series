#pragma once

#include "lob/types.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace lob {

// Same matching rules as Book, stored in tree maps + deques. The control
// group for book_bench: this is what a "reasonable STL" book looks like and
// why it does not belong on a matching core.
class StlBook {
public:
    StlBook(std::uint32_t min_px, std::uint32_t max_px, std::uint32_t max_orders)
        : min_px_(min_px), max_px_(max_px), max_orders_(max_orders) {
        if (min_px < 1 || max_px < min_px || max_orders < 1) {
            throw std::invalid_argument("StlBook: need min_px >= 1, max_px >= min_px, max_orders >= 1");
        }
    }

    [[nodiscard]] std::uint32_t live_orders() const noexcept { return live_; }
    [[nodiscard]] std::uint32_t best_bid() const noexcept {
        return bids_.empty() ? kInvalidId : bids_.begin()->first;
    }
    [[nodiscard]] std::uint32_t best_ask() const noexcept {
        return asks_.empty() ? kInvalidId : asks_.begin()->first;
    }

    [[nodiscard]] std::uint32_t qty_at(Side side, std::uint32_t px) const {
        if (side == Side::Buy) {
            const auto it = bids_.find(px);
            return it == bids_.end() ? 0 : it->second.qty;
        }
        const auto it = asks_.find(px);
        return it == asks_.end() ? 0 : it->second.qty;
    }

    std::uint32_t limit(Side side,
                        std::uint32_t px,
                        std::uint32_t qty,
                        Exec* execs,
                        std::uint32_t exec_cap,
                        std::uint32_t& nexec) {
        nexec = 0;
        if (qty == 0 || px < min_px_ || px > max_px_ || live_ >= max_orders_) {
            return kInvalidId;
        }
        const std::uint32_t id = next_id_++;
        ++live_;
        Rec rec{px, qty, side};
        rec.qty = match(id, rec, execs, exec_cap, nexec);
        if (rec.qty == 0) {
            --live_;
            return kInvalidId;
        }
        orders_.emplace(id, rec);
        rest(id, rec);
        return id;
    }

    bool cancel(std::uint32_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        const Rec rec = it->second;
        if (rec.side == Side::Buy) {
            erase_from_level(bids_, rec.px, id, rec.qty);
        } else {
            erase_from_level(asks_, rec.px, id, rec.qty);
        }
        orders_.erase(it);
        --live_;
        return true;
    }

private:
    struct Rec {
        std::uint32_t px;
        std::uint32_t qty;
        Side side;
    };

    struct Level {
        std::deque<std::uint32_t> fifo;
        std::uint32_t qty{0};
    };

    using BidMap = std::map<std::uint32_t, Level, std::greater<std::uint32_t>>;
    using AskMap = std::map<std::uint32_t, Level>;

    void rest(std::uint32_t id, Rec const& rec) {
        auto& lv = rec.side == Side::Buy ? bids_[rec.px] : asks_[rec.px];
        lv.fifo.push_back(id);
        lv.qty += rec.qty;
    }

    std::uint32_t match(std::uint32_t taker_id,
                        Rec const& taker,
                        Exec* execs,
                        std::uint32_t exec_cap,
                        std::uint32_t& nexec) {
        std::uint32_t remaining = taker.qty;
        if (taker.side == Side::Buy) {
            while (remaining != 0 && !asks_.empty() && asks_.begin()->first <= taker.px) {
                remaining = take_front(asks_, taker_id, remaining, execs, exec_cap, nexec);
            }
        } else {
            while (remaining != 0 && !bids_.empty() && bids_.begin()->first >= taker.px) {
                remaining = take_front(bids_, taker_id, remaining, execs, exec_cap, nexec);
            }
        }
        return remaining;
    }

    template <typename Map>
    std::uint32_t take_front(Map& levels,
                            std::uint32_t taker_id,
                            std::uint32_t remaining,
                            Exec* execs,
                            std::uint32_t exec_cap,
                            std::uint32_t& nexec) {
        auto it = levels.begin();
        Level& lv = it->second;
        const std::uint32_t px = it->first;
        while (remaining != 0 && !lv.fifo.empty()) {
            const std::uint32_t maker_id = lv.fifo.front();
            Rec& maker = orders_.at(maker_id);
            const std::uint32_t take = std::min(maker.qty, remaining);
            if (execs != nullptr && nexec < exec_cap) {
                execs[nexec] = Exec{maker_id, taker_id, px, take};
            }
            ++nexec;
            maker.qty -= take;
            lv.qty -= take;
            remaining -= take;
            if (maker.qty == 0) {
                lv.fifo.pop_front();
                orders_.erase(maker_id);
                --live_;
            }
        }
        if (lv.fifo.empty()) {
            levels.erase(it);
        }
        return remaining;
    }

    template <typename Map>
    static void erase_from_level(Map& levels, std::uint32_t px, std::uint32_t id, std::uint32_t qty) {
        auto lit = levels.find(px);
        if (lit == levels.end()) {
            return;
        }
        auto& dq = lit->second.fifo;
        const auto qit = std::find(dq.begin(), dq.end(), id);
        if (qit != dq.end()) {
            dq.erase(qit);
            lit->second.qty -= qty;
        }
        if (dq.empty()) {
            levels.erase(lit);
        }
    }

    std::uint32_t min_px_;
    std::uint32_t max_px_;
    std::uint32_t max_orders_;
    std::uint32_t next_id_{1};
    std::uint32_t live_{0};
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<std::uint32_t, Rec> orders_;
};

}  // namespace lob
