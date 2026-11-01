#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" -I "$root/ps5/aurora/gfx" -I "$root/.deps/aurora/lib/gfx" \
    "$root/ps5/tests/hd-visuals.cpp" -o "$work/hd-visuals"
"$work/hd-visuals"
echo 'HD camera/light/culling/region policy checks passed.'
