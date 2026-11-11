#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
using Clock = std::chrono::steady_clock;

#include "logger.hpp"
int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--soak") {
        double duration = argc > 2 ? std::atof(argv[2]) : 10;
        auto start = Clock::now();
        uint64_t iterations = 0;
        logger::Store s;
        volatile uint64_t sum = 0;
        do {
            for (int j = 0; j < 100; ++j) {
                s.add({2000, 5000});
                s.seal();
                if (!s.valid())
                    return 2;
                sum += s.queue.front()->sequence;
                s.ack(s.queue.front()->sequence);
            }
            iterations += 100;
        } while (std::chrono::duration<double>(Clock::now() - start).count() < duration);
        std::cout << std::setprecision(12) << "{\"duration_seconds\":"
                  << std::chrono::duration<double>(Clock::now() - start).count()
                  << ",\"iterations\":" << iterations << ",\"checksum\":" << sum << "}\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--frames") {
        for (int seq : {1, 2, 2, 3}) {
            char frame[80];
            int n = snprintf(frame, sizeof(frame), "DATA 000000000001 %d 2200 5000", seq);
            printf("%s %08x\n", frame, core::crc32(frame, size_t(n)));
        }
        return 0;
    }
    logger::Store s;
    std::set<uint32_t> received;
    unsigned baseline = 0, duplicates = 0, attempts = 0;
    std::cout << "wake,online,generated,baseline_delivered,final_delivered,queued,dropped,"
                 "duplicate_transmissions,attempts\n";
    for (unsigned wake = 0; wake < 120; ++wake) {
        bool online = !(wake >= 20 && wake < 40);
        s.add({int16_t(2000 + wake), 5000});
        s.seal();
        if (wake % 11 == 0) {
            logger::Store restarted;
            std::memcpy(&restarted, &s, sizeof(s));
            if (!restarted.valid())
                return 2;
            s = restarted;
        }
        if (online)
            ++baseline;
        for (unsigned limit = 0; limit < 4 && s.queue.front(); ++limit) {
            auto record = *s.queue.front();
            bool ack = false;
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                ++attempts;
                if (!online)
                    continue;
                if (!received.insert(record.sequence).second)
                    ++duplicates;
                bool ack_lost = record.sequence % 7 == 0 && attempt == 0;
                if (!ack_lost) {
                    ack = s.ack(record.sequence);
                    s.seal();
                    break;
                }
            }
            if (!ack)
                break;
        }
        std::cout << wake << ',' << online << ',' << wake + 1 << ',' << baseline << ','
                  << received.size() << ',' << s.queue.count << ',' << s.queue.drops << ','
                  << duplicates << ',' << attempts << '\n';
    }
}
