#!/usr/bin/env bash
# Apply the MoodAnchor board-support changes to an openvela source checkout.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
openvela_root=${OPENVELA_ROOT:-$(cd "$repo_dir/.." && pwd)}
nuttx_dir="$openvela_root/nuttx"
sifli_dir="$openvela_root/vendor/sifli"

if [[ ! -d "$nuttx_dir/.git" || ! -d "$sifli_dir/.git" ]]; then
  echo "错误：未找到 nuttx 或 vendor/sifli Git 仓库。" >&2
  echo "请在 repo sync 完成后运行，或设置 OPENVELA_ROOT。" >&2
  exit 2
fi

if [[ ! -f "$nuttx_dir/arch/arm/src/armv8-m/arm_doirq.c" ||
      ! -f "$sifli_dir/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig" ]]; then
  echo "错误：源码树缺少黄山派所需文件，可能是 repo sync 不完整。" >&2
  exit 2
fi

apply_one()
{
  local tree=$1
  local patch=$2
  local label=$3

  if git -C "$tree" apply --check "$patch" 2>/dev/null; then
    git -C "$tree" apply "$patch"
    echo "已应用：$label"
  elif git -C "$tree" apply --reverse --check "$patch" 2>/dev/null; then
    echo "已存在：$label"
  else
    echo "错误：$label 无法应用，源码版本或相关文件可能已改变。" >&2
    echo "目标仓库：$tree" >&2
    echo "补丁文件：$patch" >&2
    exit 1
  fi
}

apply_one "$nuttx_dir" \
  "$repo_dir/patches/nuttx-armv8m-fpscr.patch" \
  "NuttX ARMv8-M FPSCR/SysTick 修复"

apply_one "$sifli_dir" \
  "$repo_dir/patches/vendor-sifli-huangshan.patch" \
  "黄山派 GSR、供电检测、LCD 冷启动及自动面板支持"

echo "补丁准备完成：$openvela_root"
