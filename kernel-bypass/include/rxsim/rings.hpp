#pragma once

#include "rxsim/spsc_ring.hpp"

#include <cstdint>

namespace rxsim {

// App → NIC: "here is an empty buffer; DMA the next packet into it."
// Maps to an RX descriptor posted by ef_vi_receive_init + ef_vi_receive_push.
struct RxDescriptor {
    std::uint32_t buffer_id;
    std::uint64_t dma_address;
};

// NIC → App: "this buffer now contains a packet."
// Maps to an RX event from ef_eventq_poll / a completed exanic_receive_frame.
struct RxEvent {
    enum class Type : std::uint8_t { RxComplete = 1 };

    Type type;
    std::uint32_t buffer_id;
    std::uint16_t length;
};

using DescriptorRing = SpscRing<RxDescriptor>;
using EventQueue = SpscRing<RxEvent>;

}  // namespace rxsim
