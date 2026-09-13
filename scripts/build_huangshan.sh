#!/usr/bin/env bash
# Build the official SF32LB52 Huangshan Pi configuration.
#
# This wrapper now drives the official openvela build entry point
# (`./build.sh <board_config> --cmake`), the same flow the openvela quickstart
# documents for the goldfish emulator targets.  build.sh names the output
# directory after the board config, i.e. cmake_out/lckfb_huangshan_pi_nsh.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
workspace_dir=${OPENVELA_ROOT:-$(cd "$repo_dir/.." && pwd)}
jobs=${JOBS:-$(nproc)}

# prepare_huangshan.sh belongs to the legacy Make flow: it links the vendor
# Make.defs into nuttx/, which makes the CMake configure step abort with
# "Please distclean previous make build".  It is therefore opt-in only
# (HS_PREPARE=1) for people still using the Make entry point.
if [[ "${HS_PREPARE:-0}" == "1" ]]; then
  "$script_dir/prepare_huangshan.sh"
fi

if [[ ! -d "$workspace_dir/nuttx" ]]; then
  echo "error: nuttx not found under $workspace_dir" >&2
  echo "set OPENVELA_ROOT to the openvela workspace root" >&2
  exit 2
fi

board_config="$workspace_dir/vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh"
board_config_rel="vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/"
if [[ ! -d "$board_config" ]]; then
  echo "error: Huangshan Pi board config not found: $board_config" >&2
  echo "sync the complete openvela manifest first, or set OPENVELA_ROOT" >&2
  exit 2
fi
export PATH="${ARM_NONE_EABI_ROOT:-/home/wsy/.local/opt/arm-none-eabi-10.3/usr/bin}:$workspace_dir/prebuilts/build-tools/linux-x86_64/bin:$PATH"
export CROSSDEV="${CROSSDEV:-arm-none-eabi-}"

# Optional build.sh action: --menuconfig / --savedefconfig / --distclean
action=""
case "${1:-}" in
  --menuconfig|--savedefconfig|--distclean)
    action="$1"
    shift
    ;;
esac

# build.sh derives both the output directory and the board name from this
# argument, so it must stay relative to the workspace root.
(cd "$workspace_dir" && ./build.sh "$board_config_rel" --cmake $action -j"$jobs")

# build.sh derives the output directory from the board config path.  Keep the
# lookup explicit so the script also works with an older cmake_out layout.
image=""
for candidate in \
  "${HS_BUILD_DIR:-}" \
  "$workspace_dir/cmake_out/lckfb_huangshan_pi_nsh" \
  "$workspace_dir/cmake_out/lckfb_huangshan_pi"; do
  if [[ -n "$candidate" && -r "$candidate/nuttx.bin" ]]; then
    image="$candidate/nuttx.bin"
    break
  fi
done

[[ -n "$image" ]] || { echo "error: build completed without nuttx.bin" >&2; exit 1; }
echo "built: $image"
