#!/usr/bin/env bash
# Run on macOS: ps5/tools/build-runner.sh <lima-instance>
# The agent requests a build by creating .build-runner/request in the checkout.
# Requests contain no commands or arguments. Ctrl-C stops the runner.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ $(uname -s) == Darwin ]]; then
    [[ $# == 1 ]] || { echo "Usage: $0 <lima-instance>" >&2; exit 2; }
    # These paths must be mounted at the same location inside Lima.
    exec limactl shell "$1" env \
        "PS5_CLANG=${PS5_CLANG:-/usr/bin/clang-22}" \
        "LLVM_CONFIG=${LLVM_CONFIG:-/usr/bin/llvm-config-22}" \
        bash "$root/ps5/tools/build-runner.sh" --serve
fi
[[ ${1:-} == --serve && $# == 1 ]] || {
    echo "Run this script on the Mac with the Lima instance name." >&2
    exit 2
}

cd "$root"
queue="$root/.build-runner"
mkdir -p "$queue"
mkdir "$queue/lock" 2>/dev/null || {
    echo "Runner already active, or stale lock: $queue/lock" >&2
    echo "If no runner is active, remove that empty directory and retry." >&2
    exit 1
}
cleanup() {
    printf 'stopped\n' > "$queue/state"
    rmdir "$queue/lock"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Keep generated build files on the VM disk, matching the existing build setup.
export PS5_BUILD_DIR=${PS5_BUILD_DIR:-$HOME/.cache/prosperowake-build}
export PS5_CLANG=${PS5_CLANG:-/usr/bin/clang-22}
export LLVM_CONFIG=${LLVM_CONFIG:-/usr/bin/llvm-config-22}
[[ -x $PS5_CLANG && -x $LLVM_CONFIG ]] || {
    echo "Required compiler tools missing: $PS5_CLANG / $LLVM_CONFIG" >&2
    exit 1
}
rm -f "$queue/request"
printf 'idle\n' > "$queue/state"
echo "Build runner ready for $root"
echo "Build directory: $PS5_BUILD_DIR"
echo "Compiler: $PS5_CLANG; LLVM config: $LLVM_CONFIG"
echo "Requests and logs: $queue (Ctrl-C to stop)"
while :; do
    if [[ -f "$queue/request" ]]; then
        rm -f "$queue/request" "$queue/result"
        printf 'building\n' > "$queue/state"
        echo "Starting build; log: $queue/build.log"
        result=0
        bash "$root/ps5/tools/build.sh" 2>&1 | tee "$queue/build.log" || result=$?
        printf '%s\n' "$result" > "$queue/result.tmp"
        mv "$queue/result.tmp" "$queue/result"
        printf 'idle\n' > "$queue/state"
        echo "Build finished (exit $result)."
    fi
    sleep 1
done
