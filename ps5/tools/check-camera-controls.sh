#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -D__PROSPERO__ -DTARGET_PC=1 \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    "$root/ps5/tests/camera-controls.cpp" \
    "$root/ps5/src/features/pc_camera_controls.cpp" \
    -o "$work/camera-controls"
"$work/camera-controls"
"$work/camera-controls" restored
echo "PS5 camera controls checks passed (default and restored settings)."
