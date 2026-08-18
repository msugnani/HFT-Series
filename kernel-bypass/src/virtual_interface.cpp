#include "rxsim/virtual_interface.hpp"

#include <cassert>
#include <stdexcept>

namespace rxsim {

VirtualInterface::VirtualInterface(std::size_t buffer_count, std::size_t buffer_size)
    : pool_(buffer_count, buffer_size),
      rx_ring_(buffer_count),
      evq_(buffer_count),
      ownership_(buffer_count, BufferState::Free) {
    // Event ring capacity is next_pow2(buffer_count) >= buffer_count, so a
    // full set of in-flight completions cannot overflow the event queue
    // before the descriptor ring is already empty.
}

BufferState VirtualInterface::state(std::uint32_t buffer_id) const {
    if (!pool_.valid_id(buffer_id)) {
        throw std::out_of_range("buffer_id out of range");
    }
    return ownership_[buffer_id];
}

bool VirtualInterface::post_from(std::uint32_t buffer_id, BufferState expected) {
    if (!pool_.valid_id(buffer_id)) {
        return false;
    }
    if (ownership_[buffer_id] != expected) {
        // Double-post, or repost while the NIC still owns the buffer.
        return false;
    }

    const RxDescriptor desc{buffer_id, pool_.dma_address(buffer_id)};
    if (!rx_ring_.try_push(desc)) {
        return false;
    }
    ownership_[buffer_id] = BufferState::Posted;
    stats_.posted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool VirtualInterface::post(std::uint32_t buffer_id) {
    return post_from(buffer_id, BufferState::Free);
}

std::size_t VirtualInterface::post_all() {
    std::size_t n = 0;
    for (std::uint32_t id = 0; id < pool_.count(); ++id) {
        if (ownership_[id] == BufferState::Free && post(id)) {
            ++n;
        }
    }
    return n;
}

int VirtualInterface::poll(RxEvent* events, int max_events) {
    if (events == nullptr || max_events <= 0) {
        return 0;
    }
    int n = 0;
    while (n < max_events) {
        RxEvent ev{};
        if (!evq_.try_pop(ev)) {
            break;
        }
        events[n++] = ev;
    }
    return n;
}

bool VirtualInterface::repost(std::uint32_t buffer_id) {
    return post_from(buffer_id, BufferState::Filled);
}

const std::uint8_t* VirtualInterface::data(std::uint32_t buffer_id) const {
    if (!pool_.valid_id(buffer_id)) {
        return nullptr;
    }
    if (ownership_[buffer_id] != BufferState::Filled) {
        return nullptr;
    }
    return pool_.data(buffer_id);
}

bool VirtualInterface::try_acquire_rx(RxDescriptor& out) noexcept {
    return rx_ring_.try_pop(out);
}

void VirtualInterface::complete_rx(std::uint32_t buffer_id, std::uint16_t length) {
    assert(pool_.valid_id(buffer_id));
    assert(ownership_[buffer_id] == BufferState::Posted &&
           "complete_rx on a buffer that was not posted");

    ownership_[buffer_id] = BufferState::Filled;

    const RxEvent ev{RxEvent::Type::RxComplete, buffer_id, length};
    // Event ring is sized >= buffer count and each buffer is completed at
    // most once before being reposted, so this push cannot fail.
    const bool ok = evq_.try_push(ev);
    assert(ok && "event ring overflow — ring should be >= buffer count");
    (void)ok;

    stats_.completed.fetch_add(1, std::memory_order_relaxed);
}

void VirtualInterface::record_overrun() noexcept {
    stats_.rx_overrun.fetch_add(1, std::memory_order_relaxed);
    stats_.dropped.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace rxsim
