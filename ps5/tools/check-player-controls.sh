#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -D__PROSPERO__ -DTARGET_PC=1 \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    -I "$root/.deps/aurora/include" -I "$root/.deps/SDL/include" \
    "$root/ps5/tests/player-controls.cpp" \
    "$root/ps5/src/features/pc_player_controls.cpp" \
    -o "$work/player-controls"
"$work/player-controls"
echo "DualSense movement input checks passed."
