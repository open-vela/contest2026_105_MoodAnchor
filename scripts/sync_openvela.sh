#!/usr/bin/env bash
# 拉取比赛专属 manifest 所描述的完整 openvela 工程。
#
# 组委会为每个队伍提供不同的 manifest 仓库地址；不要把参赛代码仓库
# URL 当作 -u 参数。用法：
#   scripts/sync_openvela.sh <manifest-url> [manifest.xml] [workspace]
#
# 也可以通过 OPENVELA_MANIFEST_URL、OPENVELA_MANIFEST_FILE 和
# OPENVELA_ROOT 传入参数。
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
contest_repo=$(cd "$script_dir/.." && pwd)
if [[ -n "${REPO_LAUNCHER:-}" ]]; then
  repo_launcher=$REPO_LAUNCHER
elif command -v repo >/dev/null 2>&1; then
  repo_launcher=$(command -v repo)
else
  # The workspace used by this checkout keeps the downloaded launcher here.
  repo_launcher=$(cd "$contest_repo/../../../../chapt1" 2>/dev/null && pwd)/repo
fi
manifest_url=${1:-${OPENVELA_MANIFEST_URL:-}}
manifest_file=${2:-${OPENVELA_MANIFEST_FILE:-}}
workspace=${3:-${OPENVELA_ROOT:-$(cd "$contest_repo/.." && pwd)}}

if [[ -z "$manifest_url" ]]; then
  cat >&2 <<'EOF'
用法：scripts/sync_openvela.sh <组委会提供的 manifest 仓库 URL> [manifest.xml] [workspace]
例如：
  scripts/sync_openvela.sh https://github.com/<组织>/<manifest仓库>.git contest2026_105_MoodAnchor.xml /path/to/openvela

官网文档没有统一固定的 -u 地址，必须使用组委会发给本队的专属地址。
EOF
  exit 2
fi

if [[ ! -x "$repo_launcher" ]]; then
  echo "error: repo launcher not found: $repo_launcher" >&2
  echo "set REPO_LAUNCHER=/absolute/path/to/repo" >&2
  exit 2
fi

mkdir -p "$workspace"
cd "$workspace"

init_args=(init -u "$manifest_url" -b dev-ai-contest-2026)
if [[ -n "$manifest_file" ]]; then
  init_args+=(-m "$manifest_file")
fi

echo "[1/2] repo init in $workspace"
"$repo_launcher" "${init_args[@]}"
echo "[2/2] repo sync"
"$repo_launcher" sync -c -j"${REPO_JOBS:-8}"

for required in nuttx apps packages vendor; do
  [[ -d "$workspace/$required" ]] || {
    echo "error: sync finished but missing $workspace/$required" >&2
    exit 1
  }
done
echo "openvela sync complete: $workspace"

# The official manifest contains the prebuilt SiFli libraries as a separate
# project.  Keep this check explicit because a partial repo sync otherwise
# looks successful but cannot link the SF32LB52 image.
if [[ ! -d "$workspace/vendor/sifli/boards/sf32lb52/libs" ]]; then
  echo "warning: vendor/sifli/boards/sf32lb52/libs is missing; repeat repo sync or check the SiFli prebuilt project" >&2
fi
