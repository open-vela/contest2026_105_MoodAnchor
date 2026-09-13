#!/usr/bin/env python3
"""Reset an Huangshan Pi over RTS and run the non-owning smoke tests.

BLE HCI is intentionally excluded by default: the openvela Bluetooth Host
must own /dev/ttyHCI0 for a real GATT test. Pass --ble only when the Host is
disabled and the huangshan_hal_demo BLE command is the sole HCI client.
"""

import argparse
import time

import serial


def read_until(ser, marker: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(ser.read(256))
        if marker in data:
            return bytes(data)
    raise RuntimeError(f"timeout waiting for {marker!r}; received {data[-256:]!r}")


def command(ser, text: str, timeout: float = 8.0) -> bytes:
    ser.write((text + "\r\n").encode())
    output = read_until(ser, b"nsh> ", timeout)
    print(f"[{text}]\n{output.decode(errors='replace')}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--ble", action="store_true")
    parser.add_argument("--name", default="HuangshanPi")
    args = parser.parse_args()

    # CH340N RTS# is wired to the SoC active-low reset.  In pyserial,
    # rts=True asserts RTS (hold reset) and rts=False deasserts it (run).
    # Keep RTS deasserted on every exit path; leaving it asserted makes the
    # board look powered off and blanks the AMOLED panel.
    # Configure modem-control outputs before open().  Constructing Serial
    # with a port opens it immediately and the driver default may assert RTS,
    # which is a hardware reset on Huangshan Pi.
    ser = serial.Serial(port=None, baudrate=1_000_000, timeout=0.2)
    ser.port = args.port
    ser.rts = False
    ser.dtr = False
    ser.open()
    try:
        try:
            ser.rts = True
            time.sleep(0.05)
            ser.rts = False
            time.sleep(0.5)
            read_until(ser, b"nsh> ", 10.0)
            command(ser, "uname -a")
            command(ser, "ls /dev")
            command(ser, "huangshan_hal_demo i2c")
            command(ser, "huangshan_hal_demo adc")
            command(ser, "huangshan_hal_demo power")
            command(ser, "huangshan_hal_demo pwm", 12.0)
            command(ser, "huangshan_hal_demo lcd", 15.0)
            if args.ble:
                command(ser, f"huangshan_hal_demo ble_adv {args.name}", 10.0)
        finally:
            ser.rts = False
            ser.dtr = False
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
