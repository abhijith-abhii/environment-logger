#!/usr/bin/env python3
"""Durable serial gateway. Trusted local UART only; CRC is not authentication."""
import argparse
import re
import sqlite3
import sys
import zlib
PATTERN = re.compile(rb"DATA ([0-9a-f]{12}) ([0-9]{1,10}) (-?[0-9]{1,5}) ([0-9]{1,5}) ([0-9a-f]{8})")
class Gateway:
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("CREATE TABLE IF NOT EXISTS readings(device TEXT, seq INTEGER, centi_c INTEGER, centi_rh INTEGER, PRIMARY KEY(device,seq))")
        self.db.commit()
    def accept(self, line):
        if len(line) > 96:
            return None
        line = line.rstrip(b"\r\n")
        match = PATTERN.fullmatch(line)
        if not match:
            return None
        device, seq, temp, humidity, checksum = match.groups()
        payload = line.rsplit(b" ", 1)[0]
        if zlib.crc32(payload) != int(checksum, 16):
            return None
        seq, temp, humidity = int(seq), int(temp), int(humidity)
        if not 1 <= seq <= 0xffffffff or not -4000 <= temp <= 12500 or not 0 <= humidity <= 10000:
            return None
        key = (device.decode("ascii"), seq)
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO readings VALUES(?,?,?,?)", (*key, temp, humidity))
            saved = self.db.execute("SELECT centi_c,centi_rh FROM readings WHERE device=? AND seq=?", key).fetchone()
            if saved != (temp, humidity):
                return None  # Conflicting sequence reuse must not be acknowledged.
        return f"ACK {seq}\n".encode("ascii")  # Issued only after transaction commit.
    def close(self):
        self.db.close()
def serve(stream, output, gateway):
    buffer = bytearray()
    dropping = False
    while True:
        byte = stream.read(1)
        if not byte:
            if getattr(stream, "is_open", False):
                continue
            return
        if byte == b"\n":
            if not dropping:
                ack = gateway.accept(bytes(buffer))
                if ack:
                    output.write(ack)
                    output.flush()
            buffer.clear()
            dropping = False
        elif not dropping:
            if len(buffer) >= 96:
                buffer.clear()
                dropping = True
            else:
                buffer.extend(byte)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", default="readings.sqlite")
    parser.add_argument("--port", help="USB 3.3V TTL adapter attached to UART1; omitted means stdin/stdout")
    args = parser.parse_args()
    gateway = Gateway(args.db)
    try:
        if args.port:
            import serial
            with serial.Serial(args.port, 115200, timeout=0.2) as port:
                serve(port, port, gateway)
        else:
            serve(sys.stdin.buffer, sys.stdout.buffer, gateway)
    finally:
        gateway.close()
if __name__ == "__main__":
    main()
