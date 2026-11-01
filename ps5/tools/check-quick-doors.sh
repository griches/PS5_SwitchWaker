#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    "$root/ps5/tests/quick-doors.cpp" -o "$work/quick-doors"
"$work/quick-doors"
echo "Quick-door policy host checks passed."
