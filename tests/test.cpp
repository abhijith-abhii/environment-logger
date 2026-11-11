#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <vector>
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::cerr << "FAIL line " << __LINE__ << ": " << #x << "\n";                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

#include "logger.hpp"
struct Mock {
    uint8_t frame[6] = {0x66, 0x66, 0, 0x80, 0, 0};
    bool fail_write = false, fail_read = false;
    unsigned waited = 0;
    bool write(uint8_t addr, const uint8_t *p, size_t n) {
        CHECK(addr == 0x44 && n == 2 && p[0] == 0x24 && p[1] == 0);
        return !fail_write;
    }
    void wait_ms(unsigned n) {
        waited = n;
    }
    bool read(uint8_t addr, uint8_t *p, size_t n) {
        CHECK(addr == 0x44 && n == 6 && waited >= 16);
        std::memcpy(p, frame, n);
        return !fail_read;
    }
};
int main() {
    uint8_t vector[] = {0xbe, 0xef};
    CHECK(logger::crc8(vector, 2) == 0x92);
    CHECK(core::crc32("123456789", 9) == 0xcbf43926);
    Mock bus;
    bus.frame[2] = logger::crc8(bus.frame, 2);
    bus.frame[5] = logger::crc8(bus.frame + 3, 2);
    logger::Sht31<Mock> sensor{bus};
    logger::Reading r{};
    CHECK(sensor.sample(r));
    CHECK(r.centi_c == 2500 && r.centi_rh == 5000);
    for (int bit = 0; bit < 48; ++bit) {
        uint8_t bad[6];
        std::memcpy(bad, bus.frame, 6);
        bad[bit / 8] ^= 1 << (bit % 8);
        CHECK(!logger::decode(bad, 6, r));
    }
    for (size_t n = 0; n < 6; ++n)
        CHECK(!logger::decode(bus.frame, n, r));
    bus.fail_write = true;
    CHECK(!sensor.sample(r));
    bus.fail_write = false;
    bus.fail_read = true;
    CHECK(!sensor.sample(r));
    logger::Store store;
    for (unsigned i = 0; i < 32; ++i)
        CHECK(store.add({2000, 5000}));
    CHECK(!store.add({2000, 5000}));
    CHECK(store.queue.drops == 1 && store.next == 33);
    CHECK(!store.ack(99) && store.queue.count == 32);
    CHECK(store.ack(1));
    CHECK(!store.ack(1));
    CHECK(store.add({2100, 5100}));
    store.seal();
    CHECK(store.valid());
    logger::Store reboot;
    std::memcpy(&reboot, &store, sizeof(store));
    CHECK(reboot.valid() && reboot.queue.front()->sequence == 2);
    for (size_t i = 0; i < sizeof(store); ++i) {
        auto bad = store;
        reinterpret_cast<uint8_t *>(&bad)[i] ^= 1;
        CHECK(!bad.valid());
    }
    store = logger::Store{};
    store.next = UINT32_MAX;
    CHECK(store.add({0, 0}));
    CHECK(!store.add({0, 0}));
    CHECK(store.next == 0);
    uint32_t seq = 0;
    CHECK(logger::parse_ack("ACK 4294967295", seq) && seq == UINT32_MAX);
    CHECK(!logger::parse_ack("ACK 4294967296", seq));
    for (auto s : {"ACK -1", "ACK ", "ACK 1 x", "ACK +1", "ACK  1", "PPR 2"})
        CHECK(!logger::parse_ack(s, seq));
    core::Line<8> line;
    for (char c : std::string("0123456789\n"))
        CHECK(!line.feed(c));
    bool got = false;
    for (char c : std::string("ACK 1\n"))
        got |= line.feed(c);
    CHECK(got && std::string(line.data) == "ACK 1");
    core::Line<32> binary_line;
    const char binary_command[] = {'P', 'P', 'R', ' ', '2', 0, 'x', '\n'};
    for (char byte : binary_command)
        CHECK(!binary_line.feed(byte));
    std::cout << "PASS " << checks
              << " assertions: CRC, mocked I2C, overflow, ACK, restart, corrupt storage\n";
}
