#!/usr/bin/env bash
# Host checks for the PS5 gameplay settings and angle calculations.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cxx=${CXX:-c++}
"$cxx" -std=c++17 -Wall -Wextra -Werror -D__PROSPERO__ \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    "$root/ps5/tests/gameplay-options.cpp" \
    "$root/ps5/src/features/pc_gameplay_options.cpp" \
    -o "$work/gameplay-options"
"$work/gameplay-options"
"$work/gameplay-options" 1
"$work/gameplay-options" 2
"$work/gameplay-options" invalid
"$work/gameplay-options" movement
"$work/gameplay-options" grapple
"$work/gameplay-options" traversal
"$work/gameplay-options" tingle
"$work/gameplay-options" ballad
"$work/gameplay-options" intro
"$work/gameplay-options" text
"$work/gameplay-options" water
"$work/gameplay-options" jump
"$work/gameplay-options" sprint
"$work/gameplay-options" doors
"$work/gameplay-options" loading
echo "Gameplay option host checks passed (default and restored settings)."
