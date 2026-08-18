#pragma once

#include <cstdint>

namespace rxsim {

// On-wire payload used by the demo sender/receiver. Sequence lets the
// receiver count gaps independently of the NIC rx_overrun counter.
struct PacketHeader {
    std::uint64_t seq;
    std::uint64_t timestamp_ns;
};

inline constexpr std::uint16_t kMinPayload = static_cast<std::uint16_t>(sizeof(PacketHeader));

}  // namespace rxsim
