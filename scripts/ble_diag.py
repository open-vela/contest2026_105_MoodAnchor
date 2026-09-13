#!/usr/bin/env python3
"""BLE host-stack diagnostics over the 1 Mbps console.

Single-shot design: the port is opened once and never closed, because on this
board (CH340N behind VMware USB passthrough) a close/open cycle frequently
returns EIO and needs a physical replug.

Flow:
  1. open port, release DTR/RTS (open itself tends to reset the SoC)
  2. wait for NSH
  3. optionally kill the running LVGL app (it starves NSH -> 40 B/s console)
  4. run the requested command(s) and stream the output

Usage:
  scripts/ble_diag.py [--port /dev/ttyUSB0] [--no-kill] [cmd ...]
"""
import argparse
import sys
import time

import serial

BAUD = 1000000


def stream(port, secs, tag, keep=4000):
    end = time.time() + secs
    chunks = []
    while time.time() < end:
        data = port.read(4096)
        if data:
            chunks.append(data.decode("utf-8", "replace"))
    text = "".join(chunks)
    print(f"===== {tag} ({len(text)} bytes) =====", flush=True)
    print(text[-keep:] if text else "(no output)", flush=True)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--no-kill", action="store_true",
                    help="do not kill the LVGL app first")
    ap.add_argument("--boot-wait", type=float, default=8.0)
    ap.add_argument("cmds", nargs="*",
                    default=["huangshan_hal_demo blehost"])
    args = ap.parse_args()

    try:
        port = serial.Serial(args.port, BAUD, timeout=0.2)
    except serial.SerialException as exc:
        print(f"ERROR: cannot open {args.port}: {exc}")
        print("       -> unplug and replug the USB cable, then retry.")
        return 2

    port.dtr = False
    port.rts = False

    print(f"opened {args.port} @ {BAUD}; waiting {args.boot_wait:.0f}s for NSH ...",
          flush=True)
    stream(port, args.boot_wait, "BOOT")

    if not args.no_kill:
        # mood_anchor spins in the LVGL vsync loop and starves NSH,
        # which makes console I/O drop to ~40 bytes/s.
        port.write(b"kill 8\r")
        stream(port, 5, "KILL mood_anchor")

    for cmd in args.cmds:
        print(f"--> {cmd}", flush=True)
        port.write(cmd.encode() + b"\r")
        stream(port, 30, cmd)

    # deliberately NOT closing: close() tends to wedge the CH340 under VMware
    print("done (port left open on purpose)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
