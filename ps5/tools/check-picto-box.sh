#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" -I "$root/ps5/aurora" -I "$root/.deps/aurora/lib" \
    "$root/ps5/tests/picto-box.cpp" -o "$work/picto-box"
"$work/picto-box"
echo 'Picto Box gallery, crop, recognition sampling and pipeline policy host checks passed.'
