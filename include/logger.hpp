#pragma once
#include "common.hpp"
namespace logger {
inline uint8_t crc8(const uint8_t *p, size_t n) {
    uint8_t c = 0xff;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b)
            c = (c & 0x80) ? uint8_t((c << 1) ^ 0x31) : uint8_t(c << 1);
    }
    return c;
}
struct Reading {
    int16_t centi_c;
    uint16_t centi_rh;
};
inline bool decode(const uint8_t *b, size_t n, Reading &out) {
    if (n != 6 || crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5])
        return false;
    uint32_t t = uint32_t(b[0]) * 256 + b[1], h = uint32_t(b[3]) * 256 + b[4];
    int32_t tc = -4500 + int32_t((17500 * t + 32767) / 65535);
    uint32_t rh = (10000 * h + 32767) / 65535;
    if (tc < -4000 || tc > 12500 || rh > 10000)
        return false;
    out = {int16_t(tc), uint16_t(rh)};
    return true;
}
template <class Bus> struct Sht31 {
    Bus &bus;
    bool sample(Reading &out) {
        const uint8_t cmd[] = {0x24, 0x00};
        uint8_t frame[6];
        if (!bus.write(0x44, cmd, 2))
            return false;
        bus.wait_ms(20);
        return bus.read(0x44, frame, 6) && decode(frame, 6, out);
    }
};
struct Record {
    uint32_t sequence;
    int16_t centi_c;
    uint16_t centi_rh;
};
struct Store {
    uint32_t magic = 0x454e5631, version = 1, next = 1;
    core::Ring<Record, 32> queue;
    uint32_t checksum = 0;
    void seal() {
        checksum = core::crc32(this, offsetof(Store, checksum));
    }
    bool valid() const {
        return magic == 0x454e5631 && version == 1 && queue.valid() &&
               checksum == core::crc32(this, offsetof(Store, checksum));
    }
    bool add(Reading r) {
        // Sequence zero is reserved; refuse rollover rather than reusing gateway IDs.
        if (next == 0) {
            ++queue.drops;
            return false;
        }
        if (!queue.push({next, r.centi_c, r.centi_rh}))
            return false;
        ++next;
        return true;
    }
    bool ack(uint32_t seq) {
        return queue.front() && queue.front()->sequence == seq && queue.pop();
    }
};
static_assert(sizeof(Store) == 284, "Persistence format changed; migrate version");
inline bool parse_ack(const char *s, uint32_t &seq) {
    return std::strncmp(s, "ACK ", 4) == 0 && core::parse_u32(s + 4, seq);
}
} // namespace logger
