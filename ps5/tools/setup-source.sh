#!/usr/bin/env bash
# Wind Waker for the PlayStation 5 - the sources the build needs, with the console's changes.
#
#   ps5/tools/setup-source.sh    check out and patch everything; safe before every build
#
# SwitchWaker (vendor/switchwaker) is a submodule at an upstream revision. What
# the console needs changed in it is ps5/patches/switchwaker/series: PS5 hooks
# and focused native helpers. Source submodules keep the paths SwitchWaker expects under .deps/.
# Setup initializes missing checkouts once; populated builds need no network or disc:
#
#   .deps/aurora        Aurora at SwitchWaker's pin (native/cmake/Aurora.cmake),
#                       with SwitchWaker's own patches (native/patches/aurora) and
#                       then ps5/patches/aurora/series (the console's surface)
#   .deps/decomp        Asset-header source at game/UPSTREAM (also a submodule).
#   .deps/recompcore    Dolphin's DSP HLE sources (native/tools/fetch_recompcore.sh)
#   .deps/assets/<GAME_ID> regional metadata, merged into .deps/assets/universal;
#                       stub asset headers (native/tools/gen_assets.sh --stubs):
#                       names and sizes only, from the decompilation's symbol
#                       list. The arrays are read from the player's disc when
#                       the title starts (docs/RUNTIME_ASSETS.md), so nothing of
#                       the game is compiled in. Needs PyYAML.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
deps="$root/.deps"

# Missing submodules are initialized; populated trees are checked, never reset.
python3 -B "$root/ps5/tools/setup-submodules.py" --group source
python3 -B "$root/ps5/tools/prepare-source.py" --target switchwaker
python3 -B "$root/ps5/tools/prepare-source.py" --target aurora

# Both metadata tables feed the same PAL-layout executable.
python3 -B "$root/ps5/tools/prepare-asset-metadata.py"
python3 -B "$root/ps5/tools/gen-region-assets.py" \
    --us "$deps/assets/GZLE01" --pal "$deps/assets/GZLP01" --out "$deps/assets/universal"
