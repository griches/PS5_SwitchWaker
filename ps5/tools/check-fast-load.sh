#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    "$root/ps5/tests/fast-load.cpp" -o "$work/fast-load"
"$work/fast-load"
echo "Covered-screen loading policy host checks passed."
