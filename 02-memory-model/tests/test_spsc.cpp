#include "hft/spsc_ring.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

void test_empty_full() {
    hft::SpscRing<int> ring(4);
    require(ring.capacity() == 4, "capacity should stay 4");
    require(ring.empty(), "new ring is empty");

    require(ring.try_push(1), "push 1");
    require(ring.try_push(2), "push 2");
    require(ring.try_push(3), "push 3");
    require(ring.try_push(4), "push 4");
    require(!ring.try_push(5), "fifth push must fail");
    require(ring.full(), "ring is full");

    int v = 0;
    require(ring.try_pop(v) && v == 1, "pop 1");
    require(ring.try_push(5), "reuse slot after pop");
    require(ring.try_pop(v) && v == 2, "pop 2");
    require(ring.try_pop(v) && v == 3, "pop 3");
    require(ring.try_pop(v) && v == 4, "pop 4");
    require(ring.try_pop(v) && v == 5, "pop 5");
    require(!ring.try_pop(v), "empty pop fails");
}

void test_rounds_up_capacity() {
    hft::SpscRing<int> ring(5);
    require(ring.capacity() == 8, "capacity rounds up to power of two");
}

void test_two_threads() {
    constexpr int kCount = 200000;
    hft::SpscRing<std::uint64_t> ring(1024);

    std::uint64_t produced_sum = 0;
    std::thread producer([&] {
        for (int i = 1; i <= kCount; ++i) {
            const auto value = static_cast<std::uint64_t>(i);
            produced_sum += value;
            while (!ring.try_push(value)) {
            }
        }
    });

    std::uint64_t consumed_sum = 0;
    int consumed = 0;
    std::thread consumer([&] {
        while (consumed < kCount) {
            std::uint64_t value = 0;
            if (ring.try_pop(value)) {
                consumed_sum += value;
                ++consumed;
            }
        }
    });

    producer.join();
    consumer.join();

    require(consumed == kCount, "consumer saw every item");
    require(consumed_sum == produced_sum, "payloads match across the acquire/release pair");
    require(ring.empty(), "ring empty after the run");
}

}  // namespace

int main() {
    try {
        test_empty_full();
        test_rounds_up_capacity();
        test_two_threads();
    } catch (const std::exception& ex) {
        std::cerr << "test_spsc_hft FAILED: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "test_spsc_hft OK\n";
    return 0;
}
