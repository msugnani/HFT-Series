#pragma once

#include "hft/common.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace hft {

// Preallocated slab + intrusive free stack. Not thread-safe: give each hot
// thread its own pool, or protect with a ring of buffer ids (see kernel-bypass).
template <typename T>
class Pool {
public:
    explicit Pool(std::size_t n) : storage_(n), free_(n) {
        if (n == 0) {
            throw std::invalid_argument("Pool size must be > 0");
        }
        for (std::size_t i = 0; i < n; ++i) {
            free_[i] = static_cast<std::uint32_t>(n - 1 - i);
        }
        top_ = n;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t available() const noexcept { return top_; }

    [[nodiscard]] T* try_acquire() noexcept {
        if (top_ == 0) {
            return nullptr;
        }
        const std::uint32_t id = free_[--top_];
        return &storage_[id];
    }

    void release(T* p) noexcept {
        const auto id = static_cast<std::uint32_t>(p - storage_.data());
        free_[top_++] = id;
    }

    [[nodiscard]] std::uint32_t id_of(T const* p) const noexcept {
        return static_cast<std::uint32_t>(p - storage_.data());
    }

    [[nodiscard]] T* at(std::uint32_t id) noexcept { return &storage_[id]; }

private:
    std::vector<T> storage_;
    std::vector<std::uint32_t> free_;
    std::size_t top_{0};
};

}  // namespace hft
