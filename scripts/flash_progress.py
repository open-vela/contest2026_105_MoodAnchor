#!/usr/bin/env python3
"""Flash an SF32LB52 image with a live progress bar and stall detection.

Why this exists: sftool prints bare percentages ("0%", "37%", ...) on a single
line, so when the CH340N link dies mid-transfer it just stops printing and the
terminal looks identical to a slow transfer.  This wrapper renders a real
progress bar, shows throughput, and reports how long nothing has changed so a
dead link is obvious immediately.

Exit code is sftool's exit code (0 = success).
"""

import argparse
import os
import re
import select
import subprocess
import sys
import time

BAR_WIDTH = 30
MIB = 1024 * 1024


def render(pct, elapsed, size, note=""):
    filled = int(BAR_WIDTH * pct / 100)
    bar = "█" * filled + "░" * (BAR_WIDTH - filled)
    speed = (size * pct / 100) / elapsed / MIB if elapsed > 0 and pct else 0.0
    line = f"[{bar}] {pct:3d}%  {speed:5.2f} MiB/s  {elapsed:6.1f}s"
    if note:
        line += f"  {note}"
    print("\r" + line[:110].ljust(110), end="", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="nuttx.bin to write to NOR @0x12010000")
    parser.add_argument("-p", "--port", default="/dev/ttyUSB0")
    parser.add_argument("-b", "--baud", default="1000000")
    parser.add_argument(
        "--stall",
        type=float,
        default=20.0,
        help="seconds without any progress before declaring the link dead",
    )
    parser.add_argument(
        "--before",
        default="no_reset",
        help="sftool --before reset mode (default: no_reset)",
    )
    parser.add_argument(
        "--after",
        default="no_reset",
        help="sftool --after reset mode (default: no_reset)",
    )
    parser.add_argument(
        "--compat",
        default="",
        help="value for sftool --compat; empty means do not pass it "
        "(compat mode is slower, only needed when transfers are flaky)",
    )
    args = parser.parse_args()

    if not os.access(args.image, os.R_OK):
        print(f"error: cannot read image {args.image}", file=sys.stderr)
        return 2

    size = os.path.getsize(args.image)
    cmd = [
        "sftool", "-c", "SF32LB52",
        "-p", args.port,
        "-b", args.baud,
        "--before", args.before,
        "--after", args.after,
    ]

    if args.compat:
        cmd += ["--compat", args.compat]

    cmd += ["write_flash", f"{args.image}@0x12010000"]

    print(f"image : {args.image}")
    print(f"size  : {size / MIB:.2f} MiB")
    print(f"port  : {args.port} @ {args.baud} baud   (stall timeout {args.stall:.0f}s)")
    print(f"reset : --before {args.before} --after {args.after}"
          f"   compat: {args.compat or 'off'}")
    print("-" * 72)

    start = time.time()
    last_change = start
    last_pct = -1
    other_lines = []

    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0
    )
    assert proc.stdout is not None

    buf = b""
    killed = False
    try:
        while True:
            ready, _, _ = select.select([proc.stdout], [], [], 0.5)
            if ready:
                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                buf += chunk
                # sftool separates updates with \r and/or \n
                while True:
                    m = re.search(rb"[\r\n]", buf)
                    if not m:
                        break
                    text = buf[: m.start()].decode("utf-8", "replace").strip()
                    buf = buf[m.end():]
                    if not text:
                        continue
                    pct_match = re.match(r"^(\d+)%$", text)
                    if pct_match:
                        pct = int(pct_match.group(1))
                        if pct != last_pct:
                            last_pct = pct
                            last_change = time.time()
                            render(pct, time.time() - start, size)
                    else:
                        other_lines.append(text)
                        print("\r" + text[:110].ljust(110), flush=True)

            if proc.poll() is not None:
                break

            idle = time.time() - last_change
            if idle > args.stall:
                print(
                    f"\n⚠️  no progress for {idle:.0f}s (stuck at {max(last_pct, 0)}%)"
                    " - the USB/UART link is dead, unplug + replug the board"
                )
                proc.kill()
                killed = True
                break
    finally:
        rc = proc.wait()

    elapsed = time.time() - start
    print()
    print("-" * 72)

    if rc == 0 and not killed:
        print(f"✅ flash OK: {size / MIB:.2f} MiB in {elapsed:.1f}s")
    else:
        print(f"❌ flash FAILED (rc={rc}{', killed on stall' if killed else ''}) "
              f"after {elapsed:.1f}s at {max(last_pct, 0)}%")
        for line in other_lines[-5:]:
            print("   " + line[:100])
    return rc


if __name__ == "__main__":
    sys.exit(main())
