#!/usr/bin/env bash
# Flash an SF32LB52 Huangshan Pi image using SiFli sftool.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
workspace_dir=${OPENVELA_ROOT:-$(cd "$repo_dir/.." && pwd)}
port=${1:-${HS_SERIAL_PORT:-/dev/ttyUSB0}}
baud=${HS_FLASH_BAUD:-1000000}

# The official build entry `./build.sh <board_config> --cmake` names the output
# directory after the board config (cmake_out/lckfb_huangshan_pi_nsh), while an
# older hand-written cmake invocation produced cmake_out/lckfb_huangshan_pi.
# Pick whichever exists; HS_IMAGE (or argv 2) always overrides.
default_image=""
for candidate in \
  "$workspace_dir/cmake_out/lckfb_huangshan_pi_nsh/nuttx.bin" \
  "$workspace_dir/cmake_out/lckfb_huangshan_pi/nuttx.bin"; do
  if [[ -r "$candidate" ]]; then
    default_image="$candidate"
    break
  fi
done

image=${2:-${HS_IMAGE:-$default_image}}

command -v sftool >/dev/null || {
  echo "error: sftool not found (download a release or run: cargo install sftool)" >&2
  exit 2
}
python3 -c 'import serial' 2>/dev/null || {
  echo "error: pyserial is required for the safe RTS reset sequence" >&2
  exit 2
}
[[ -r "$image" ]] || { echo "error: image not found: $image" >&2; exit 2; }

# CH340N RTS# is wired to the active-low SoC reset.
#
# Why the resets are done by hand instead of by sftool:
#   * sftool --before default_reset is NOT compatible with this board - it
#     panics with `I/O error` at 0%.
#   * sftool --after soft_reset downloads a stub, which fails here with
#     `Failed to download stub: Timeout("receiving UART frame")`.
# So the sequence is: pulse RTS (catch the ROM loader window) -> sftool writes
# with both resets disabled -> pulse RTS again to boot the new image.
#
# Note: every extra open of /dev/ttyUSB0 is a risk on this VMware-passthrough
# setup (a second open often returns EIO), so do not add probe/diagnostic
# opens around this script - run it right after a replug / usbreset.
python3 - "$port" "$baud" <<'PY'
import serial
import sys
import time

ser = serial.Serial(port=None, baudrate=int(sys.argv[2]), timeout=0.2)
ser.port = sys.argv[1]
ser.rts = False
ser.dtr = False
ser.open()
try:
    ser.rts = True
    time.sleep(0.05)
    ser.rts = False
    time.sleep(0.15)
finally:
    ser.rts = False
    ser.dtr = False
    ser.close()
PY

python3 "$script_dir/flash_progress.py" "$image" \
  -p "$port" -b "$baud" --stall "${HS_FLASH_STALL:-20}" \
  --before "${HS_FLASH_BEFORE:-no_reset}" \
  --after "${HS_FLASH_AFTER:-no_reset}" \
  --compat "${HS_FLASH_COMPAT:-}"
flash_status=$?

# Boot the freshly written image (RTS-to-RST pulse).
python3 - "$port" <<'PY'
import serial
import sys
import time

ser = serial.Serial(port=None, baudrate=1000000, timeout=0.2)
ser.port = sys.argv[1]
ser.rts = False
ser.dtr = False
ser.open()
try:
    ser.rts = True
    time.sleep(0.05)
    ser.rts = False
    time.sleep(0.2)
finally:
    ser.rts = False
    ser.dtr = False
    ser.close()
PY

exit "$flash_status"
