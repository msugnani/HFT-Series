#include "hft/common.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Packed stream: [u16 len][u8 type][payload]
// type 1 = trade (px u32, qty u32), type 2 = heartbeat (ts u64)

namespace {

enum class Type : std::uint8_t { Trade = 1, Heartbeat = 2 };

#pragma pack(push, 1)
struct Hdr {
    std::uint16_t len;
    std::uint8_t type;
};
struct Trade {
    std::uint32_t px;
    std::uint32_t qty;
};
struct Heartbeat {
    std::uint64_t ts;
};
#pragma pack(pop)

std::vector<std::uint8_t> build(std::uint64_t n) {
    std::vector<std::uint8_t> buf;
    buf.reserve(static_cast<std::size_t>(n) * 16);
    for (std::uint64_t i = 0; i < n; ++i) {
        if ((i & 7u) == 0) {
            const Hdr h{static_cast<std::uint16_t>(sizeof(Hdr) + sizeof(Heartbeat)),
                        static_cast<std::uint8_t>(Type::Heartbeat)};
            const Heartbeat hb{i};
            const auto off = buf.size();
            buf.resize(off + h.len);
            std::memcpy(buf.data() + off, &h, sizeof(h));
            std::memcpy(buf.data() + off + sizeof(h), &hb, sizeof(hb));
        } else {
            const Hdr h{static_cast<std::uint16_t>(sizeof(Hdr) + sizeof(Trade)),
                        static_cast<std::uint8_t>(Type::Trade)};
            const Trade t{static_cast<std::uint32_t>(i), 1};
            const auto off = buf.size();
            buf.resize(off + h.len);
            std::memcpy(buf.data() + off, &h, sizeof(h));
            std::memcpy(buf.data() + off + sizeof(h), &t, sizeof(t));
        }
    }
    return buf;
}

std::uint64_t parse_inplace(std::uint8_t const* p, std::uint8_t const* end) {
    std::uint64_t sink = 0;
    while (p + sizeof(Hdr) <= end) {
        Hdr h{};
        std::memcpy(&h, p, sizeof(h));
        if (h.len < sizeof(Hdr) || p + h.len > end) {
            break;
        }
        const std::uint8_t* payload = p + sizeof(Hdr);
        if (h.type == static_cast<std::uint8_t>(Type::Trade) &&
            h.len >= sizeof(Hdr) + sizeof(Trade)) {
            Trade t{};
            std::memcpy(&t, payload, sizeof(t));
            sink += t.px + t.qty;
        } else if (h.type == static_cast<std::uint8_t>(Type::Heartbeat) &&
                   h.len >= sizeof(Hdr) + sizeof(Heartbeat)) {
            Heartbeat hb{};
            std::memcpy(&hb, payload, sizeof(hb));
            sink += hb.ts;
        }
        p += h.len;
    }
    return sink;
}

std::uint64_t parse_copy(std::uint8_t const* p, std::uint8_t const* end) {
    std::uint64_t sink = 0;
    while (p + sizeof(Hdr) <= end) {
        Hdr h{};
        std::memcpy(&h, p, sizeof(h));
        if (h.len < sizeof(Hdr) || p + h.len > end) {
            break;
        }
        std::string copy(reinterpret_cast<char const*>(p), h.len);
        Hdr h2{};
        std::memcpy(&h2, copy.data(), sizeof(h2));
        const auto* payload = reinterpret_cast<std::uint8_t const*>(copy.data() + sizeof(Hdr));
        if (h2.type == static_cast<std::uint8_t>(Type::Trade)) {
            Trade t{};
            std::memcpy(&t, payload, sizeof(t));
            sink += t.px + t.qty;
        } else if (h2.type == static_cast<std::uint8_t>(Type::Heartbeat)) {
            Heartbeat hb{};
            std::memcpy(&hb, payload, sizeof(hb));
            sink += hb.ts;
        }
        p += h.len;
    }
    return sink;
}

}  // namespace

int main(int argc, char** argv) {
    const hft::Argv args{argc, argv};
    const auto n = args.u64("--n", 400000);
    const int copy = args.i32("--copy", 0);

    const auto buf = build(n);
    auto* begin = buf.data();
    auto* end = buf.data() + buf.size();

    std::uint64_t sink = copy ? parse_copy(begin, end) : parse_inplace(begin, end);
    hft::do_not_optimize(sink);

    const std::uint64_t t0 = hft::now_ns();
    sink = copy ? parse_copy(begin, end) : parse_inplace(begin, end);
    const std::uint64_t t1 = hft::now_ns();
    hft::do_not_optimize(sink);

    std::cout << "n=" << n << " bytes=" << buf.size() << " copy=" << copy
              << " elapsed_ns=" << (t1 - t0)
              << " ns_per=" << (static_cast<double>(t1 - t0) / static_cast<double>(n))
              << " sink=" << sink << '\n';
    return 0;
}
