#include "config.example.hpp"
#include "logger.hpp"
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
struct I2cBus {
    bool write(uint8_t a, const uint8_t *b, size_t n) {
        Wire.beginTransmission(a);
        Wire.write(b, n);
        return Wire.endTransmission() == 0;
    }
    void wait_ms(unsigned ms) {
        delay(ms);
    }
    bool read(uint8_t a, uint8_t *b, size_t n) {
        if (Wire.requestFrom(a, uint8_t(n)) != n)
            return false;
        for (size_t i = 0; i < n; ++i)
            b[i] = Wire.read();
        return true;
    }
};
Preferences prefs;
logger::Store store;
bool storage_ready = false;
bool save() {
    store.seal();
    return storage_ready && prefs.putBytes("queue", &store, sizeof(store)) == sizeof(store);
}
bool send_record(const logger::Record &r) {
    core::Line<32> line;
    char payload[80];
    // Device ID makes independent boards distinct; sequence survives ordinary reset.
    uint64_t id = ESP.getEfuseMac();
    int n = snprintf(payload, sizeof(payload), "DATA %012llx %lu %d %u", (unsigned long long)id,
                     (unsigned long)r.sequence, r.centi_c, r.centi_rh);
    uint32_t crc = core::crc32(payload, size_t(n));
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        esp_task_wdt_reset();
        Serial1.printf("%s %08lx\n", payload, (unsigned long)crc);
        uint32_t start = millis();
        while (core::elapsed(millis(), start) < 250) {
            // A byte budget prevents a noisy sender from extending the deadline indefinitely.
            for (unsigned budget = 0; budget < 64 && Serial1.available(); ++budget) {
                char c = char(Serial1.read());
                uint32_t seq;
                if (line.feed(c) && logger::parse_ack(line.data, seq) && seq == r.sequence)
                    return true;
            }
            delay(1);
        }
        line.reset();
    }
    return false;
}
void setup() {
    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, 16, 17);
    Wire.begin(21, 22, 100000);
    Wire.setTimeOut(50);
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(nullptr);
    storage_ready = prefs.begin("env", false);
    bool valid = storage_ready;
    if (storage_ready && prefs.isKey("queue")) {
        valid = prefs.getBytesLength("queue") == sizeof(store) &&
                prefs.getBytes("queue", &store, sizeof(store)) == sizeof(store) && store.valid();
    }
    // Corrupt state must not silently reuse IDs and overwrite old gateway records.
    if (!valid) {
        Serial.println("FAULT persistent store invalid; repair explicitly, no transmission");
    } else {
        logger::Reading r;
        bool ok = false;
#ifdef SIMULATED_SENSORS
        r = {int16_t(2200 + store.next % 100), 5000};
        ok = true;
#else
        I2cBus bus;
        logger::Sht31<I2cBus> sensor{bus};
        ok = sensor.sample(r);
#endif
        if (ok)
            store.add(r);
        else
            Serial.println("FAULT sensor missing or CRC invalid");
        if (save()) {
            for (unsigned n = 0; n < MAX_RECORDS_PER_WAKE && store.queue.front(); ++n) {
                logger::Record record = *store.queue.front();
                if (!send_record(record))
                    break;
                store.ack(record.sequence);
                if (!save()) {
                    Serial.println("FAULT persistence after ACK; replay safe");
                    break;
                }
            }
        } else
            Serial.println("FAULT persistence before send; transmission skipped");
        Serial.printf("queued=%u dropped=%u next=%u\n", store.queue.count, store.queue.drops,
                      store.next);
    }
    prefs.end();
    Serial.flush();
    Serial1.flush();
    esp_task_wdt_delete(nullptr);
    esp_sleep_enable_timer_wakeup(uint64_t(SLEEP_SECONDS) * 1000000);
    esp_deep_sleep_start();
}
void loop() {}
