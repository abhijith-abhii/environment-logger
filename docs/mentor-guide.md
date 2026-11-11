# Understanding environmental telemetry logger

Each wake reads the sensor once, checks its CRC and saves a record. The oldest saved record is sent first. The computer gateway commits it to SQLite before acknowledging its sequence number. If that acknowledgment is lost, the device repeats the record; the database recognizes the sequence and avoids a second row. The ESP32 then sleeps for 60 seconds.

## Code walkthrough

### include/logger.hpp: crc8 / decode

Compare the datasheet BEEF -> 92 vector, then follow rounded integer conversion into centi-degrees and centi-percent.

### include/logger.hpp: Store

Explain ring ownership, sequence allocation, seal/valid and why ACK must remove only the oldest entry.

### src/main.cpp: setup

Follow read, enqueue, persist, send, ACK, persist, sleep. Explain both reset windows around ACK handling.

### src/main.cpp: send_record

Show device ID, CRC payload, timeout subtraction, attempt limit and parser byte budget.

### tools/gateway.py: Gateway.accept

Explain database primary key and transaction completion before ACK. CRC is checked before any database mutation.

### tests/test_gateway.py

This executes actual local processes and SQLite. It does not model ESP32 flash physics.

## Ten interview questions

### 1. Why persist before sending?

If power fails after sending but before local persistence, the gateway might have a record whose ID the device later reuses. Persisting first preserves allocation and replay state.

### 2. Why can duplicates occur?

The gateway may commit a record and its ACK may be lost. The sender cannot distinguish that from data loss, so it retries.

### 3. Is this exactly-once delivery?

No. Transmission is at least once within the configured retry/storage bounds. The gateway uses a primary key to store one row per device and sequence.

### 4. Why is CRC needed on I2C?

An ACK confirms a byte transfer, not the integrity of the sensor measurement. SHT31 supplies CRC bytes for its data words.

### 5. What if the queue is full?

The newest sample is rejected and the loss counter increments. Older unacknowledged records retain their identity and order.

### 6. How is battery life estimated?

Measure average current across complete cycles first. Usable battery capacity divided by average current is an idealized estimate; derating, regulator efficiency and self-discharge must be explicit. No life figure is claimed here.

### 7. What if sequence numbers wrap?

Zero is reserved. After UINT32_MAX is allocated, new records are refused instead of reusing gateway identities. An explicit reprovisioning scheme would be a future improvement.

### 8. What happens after a corrupt store?

The firmware emits a fault and does not send. Silent reset to an empty queue could reuse IDs and hide lost records.

### 9. Why not Wi-Fi or MQTT?

A local UART gateway makes reliability and persistence testable without infrastructure. It isolates the protocol learning objective; a later radio adapter can preserve the same queue semantics.

### 10. What is the main deployment limitation?

The board power budget, flash wear, timestamps and physical link all need improvement or measurement. Host success establishes logic, not battery operation or environmental durability.

## Five minute demonstration

1. Run sh tools/test.sh and sh tools/simulate.sh.
2. Run python3 tests/test_gateway.py; show the actual process restart and duplicate tests.
3. Inspect trace.csv at wakes 20, 39 and after reconnect. Compare retained records and baseline losses.
4. Explain the ACK-loss duplicate counter and why the database still has one row per ID.

## Independent exercises

- Add an explicit provisioning epoch to device identity so erased devices can safely restart sequences.
- Batch NVS updates or use FRAM, then quantify endurance and recovery tradeoffs.
- Add a real-time clock and version the wire/store formats without breaking old data.

## Resume wording and contribution

These describe the implemented artifact and recorded tests. The code was generated with AI assistance in this session; do not present it as independently authored or physically validated. Use these bullets only after you can reproduce and explain the implementation, and retain the AI-assisted description where it accurately reflects your contribution.

- Built an AI-assisted C++ environmental logger with CRC-checked SHT31 transactions, a 32-record persistent queue and bounded acknowledged UART delivery.
- Recovered 120 of 120 generated readings in a deterministic 20-wake outage simulation, versus 100 of 120 with fire-and-forget delivery.
- Implemented and tested a SQLite gateway that commits before ACK and deduplicates retransmissions across process restarts; ESP32 firmware cross-compiled.

For a later hardware achievement, first execute the numbered experiments in hardware.md, retain raw captures and revise the bullet with the actual sample count and measured result. No physical percentage, battery lifetime, throughput or accuracy is supplied as a placeholder.
