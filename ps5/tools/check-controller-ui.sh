#!/usr/bin/env bash
# Host-only tests of disc UI transformations. Not part of check-source.sh.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/ps5/include" \
    "$root/ps5/tests/controller-ui-transform.cpp" \
    "$root/ps5/src/integration/pc_ui_transform.cpp" -o "$work/controller-ui"
"$work/controller-ui"
