#!/usr/bin/env python3
"""Capture the MoodAnchor console while a phone connects over BLE.

The CH340 adapter used for this board drops off the USB bus fairly often, and
opening the port asserts RTS, which resets the SoC.  So: RTS/DTR are cleared
right after open, and the read loop reopens the port when the adapter comes
back instead of dying.

The output goes to stdout and, with --log, to a timestamped file.  Nothing is
parsed or filtered: when the firmware hangs or asserts, the interesting part
is usually the last few lines before the silence, so raw is what we want.

Usage:
    python3 scripts/ble_capture.py --log logs/ble-crash.txt --seconds 180
"""

import argparse
import os
import sys
import time

import serial


def stamp() -> str:
    return time.strftime("%H:%M:%S")


def capture(port: str, baud: int, seconds: float, log) -> None:
    deadline = time.time() + seconds
    total = 0

    while time.time() < deadline:
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = baud
            ser.bytesize = serial.EIGHTBITS
            ser.parity = serial.PARITY_NONE
            ser.stopbits = serial.STOPBITS_ONE
            ser.timeout = 0.25

            # Keep the board running: the adapter wires RTS to reset.
            ser.dtr = False
            ser.rts = False
            ser.open()
            ser.dtr = False
            ser.rts = False
        except Exception as exc:                     # noqa: BLE001
            print(f"[{stamp()}] open failed: {exc}", file=sys.stderr)
            time.sleep(1.0)
            continue

        print(f"[{stamp()}] listening on {port} @ {baud}", file=sys.stderr)
        log.write(f"\n=== [{stamp()}] port opened ===\n")
        log.flush()

        # Silence is the interesting signal: a frozen firmware stops talking
        # while the capture is still perfectly healthy.  Without a marker the
        # log just ends, and that reads the same as a dead adapter.

        last_data = time.time()
        quiet_reported = False

        try:
            while time.time() < deadline:
                data = ser.read(4096)
                if data:
                    total += len(data)
                    last_data = time.time()
                    quiet_reported = False
                    text = data.decode("utf-8", "replace")
                    log.write(f"[{stamp()}] {text}")
                    log.flush()
                elif not quiet_reported and time.time() - last_data > 6.0:
                    quiet_reported = True
                    log.write(f"\n=== [{stamp()}] NO OUTPUT for 6 s ===\n")
                    log.flush()
        except Exception as exc:                     # noqa: BLE001
            print(f"[{stamp()}] read failed: {exc}", file=sys.stderr)
            log.write(f"\n=== [{stamp()}] port lost: {exc} ===\n")
            log.flush()
        finally:
            try:
                ser.close()
            except Exception:                        # noqa: BLE001
                pass

        if time.time() < deadline:
            time.sleep(1.0)

    print(f"[{stamp()}] done, {total} bytes captured", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=1000000)
    parser.add_argument("--seconds", type=float, default=180.0)
    parser.add_argument("--log", default=None)
    args = parser.parse_args()

    if args.log:
        os.makedirs(os.path.dirname(args.log), exist_ok=True)
        with open(args.log, "w", encoding="utf-8") as log:
            capture(args.port, args.baud, args.seconds, log)
    else:
        capture(args.port, args.baud, args.seconds, sys.stdout)

    return 0


if __name__ == "__main__":
    sys.exit(main())
