#pragma once

#include "rxsim/spsc_ring.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace rxsim {

inline constexpr std::size_t kDefaultBufferSize = 2048;

// Preallocated slab of fixed-size packet buffers.
//
// buffer_id is the index. dma_address() is just the virtual pointer cast to
// uint64_t — the pedagogical stand-in for a DMA address. On real ef_vi this
// memory would be registered/pinned (ef_memreg) so the NIC can DMA into it.
class PacketPool {
public:
    PacketPool(std::size_t buffer_count, std::size_t buffer_size = kDefaultBufferSize)
        : count_(buffer_count),
          size_(buffer_size),
          stride_(align_up(buffer_size, kCacheLine)) {
        if (buffer_count == 0) {
            throw std::invalid_argument("PacketPool buffer_count must be > 0");
        }
        if (buffer_size == 0) {
            throw std::invalid_argument("PacketPool buffer_size must be > 0");
        }
        slab_ = std::make_unique<std::uint8_t[]>(count_ * stride_);
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::size_t buffer_size() const noexcept { return size_; }
    [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

    [[nodiscard]] std::uint8_t* data(std::uint32_t buffer_id) noexcept {
        return slab_.get() + static_cast<std::size_t>(buffer_id) * stride_;
    }

    [[nodiscard]] const std::uint8_t* data(std::uint32_t buffer_id) const noexcept {
        return slab_.get() + static_cast<std::size_t>(buffer_id) * stride_;
    }

    // Fake DMA address: the CPU virtual pointer. Real hardware would use a
    // bus/IOVA address from a memory-registration call.
    [[nodiscard]] std::uint64_t dma_address(std::uint32_t buffer_id) const noexcept {
        return reinterpret_cast<std::uint64_t>(data(buffer_id));
    }

    [[nodiscard]] bool valid_id(std::uint32_t buffer_id) const noexcept {
        return buffer_id < count_;
    }

private:
    static constexpr std::size_t align_up(std::size_t value, std::size_t align) noexcept {
        return (value + align - 1) & ~(align - 1);
    }

    std::size_t count_;
    std::size_t size_;
    std::size_t stride_;
    std::unique_ptr<std::uint8_t[]> slab_;
};

}  // namespace rxsim
