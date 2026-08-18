#pragma once

#include "rxsim/packet_pool.hpp"
#include "rxsim/rings.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rxsim {

enum class BufferState : std::uint8_t {
    Free = 0,
    Posted = 1,  // NIC owns it (posted on the descriptor ring)
    Filled = 2,  // App owns it (DMA done; safe to read after poll)
};

struct ViStats {
    std::atomic<std::uint64_t> posted{0};
    std::atomic<std::uint64_t> completed{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> rx_overrun{0};
};

// Application-facing receive interface.
//
//   post   ~ ef_vi_receive_init + receive_push
//   poll   ~ ef_eventq_poll / exanic_receive_frame
//   repost ~ receive_init again after processing
//
// Ownership:
//   Free → Posted (post/repost) → Filled (NIC complete) → Free/Posted (repost)
// Double-post, or reading a buffer the NIC still owns, is a hard error.
class VirtualInterface {
public:
    explicit VirtualInterface(std::size_t buffer_count,
                              std::size_t buffer_size = kDefaultBufferSize);

    VirtualInterface(const VirtualInterface&) = delete;
    VirtualInterface& operator=(const VirtualInterface&) = delete;

    [[nodiscard]] PacketPool& pool() noexcept { return pool_; }
    [[nodiscard]] const PacketPool& pool() const noexcept { return pool_; }
    [[nodiscard]] ViStats& stats() noexcept { return stats_; }
    [[nodiscard]] const ViStats& stats() const noexcept { return stats_; }

    [[nodiscard]] std::size_t buffer_count() const noexcept { return pool_.count(); }
    [[nodiscard]] BufferState state(std::uint32_t buffer_id) const;

    // App API ----------------------------------------------------------------
    // Post a Free buffer to the NIC. Returns false on ownership / ring errors.
    bool post(std::uint32_t buffer_id);

    // Post every Free buffer. Returns how many were posted.
    std::size_t post_all();

    // Drain up to max_events completions. Returns the number written.
    int poll(RxEvent* events, int max_events);

    // Return a Filled buffer to the NIC. Equivalent to post() after processing.
    bool repost(std::uint32_t buffer_id);

    // Readable only while Filled (after the corresponding poll).
    [[nodiscard]] const std::uint8_t* data(std::uint32_t buffer_id) const;

    // NIC-facing API (SimulatedNic or a test producer) -----------------------
    bool try_acquire_rx(RxDescriptor& out) noexcept;
    void complete_rx(std::uint32_t buffer_id, std::uint16_t length);
    void record_overrun() noexcept;

private:
    bool post_from(std::uint32_t buffer_id, BufferState expected);

    PacketPool pool_;
    DescriptorRing rx_ring_;
    EventQueue evq_;
    std::vector<BufferState> ownership_;
    ViStats stats_;
};

}  // namespace rxsim
