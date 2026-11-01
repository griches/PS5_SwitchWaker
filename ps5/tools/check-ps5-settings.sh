#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -D__PROSPERO__ \
    -I "$root/ps5/tests/stubs/settings" -I "$root/ps5/include" -I "$root/vendor/switchwaker/native/include" \
    "$root/ps5/tests/ps5-settings.cpp" \
    "$root/vendor/switchwaker/native/src/pc/features/pc_settings.cpp" \
    -Wl,--wrap=open,--wrap=rename,--wrap=fflush -o "$work/settings"
for scenario in conflict plain active inactive handheld dev-handheld locked fresh backup-failure rename-failure flush-failure; do
    mkdir "$work/$scenario"
    "$work/settings" "$scenario" "$work/$scenario"
done
echo "PS5 settings migration, persistence, I/O failure and menu visibility checks passed."
