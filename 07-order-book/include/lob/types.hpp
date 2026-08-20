#pragma once

#include <cstdint>

namespace lob {

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

inline constexpr std::uint32_t kInvalidId = 0;

// One match event. Price is the maker's resting price (price-time priority).
struct Exec {
    std::uint32_t maker_id;
    std::uint32_t taker_id;
    std::uint32_t px;
    std::uint32_t qty;
};

}  // namespace lob
