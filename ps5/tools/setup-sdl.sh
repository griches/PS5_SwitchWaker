#!/usr/bin/env bash
# Wind Waker for the PlayStation 5 - SDL3 at the release Aurora pins, patched for the console.
#
#   ps5/tools/setup-sdl.sh       fetch it into .deps/SDL (once a release), print the path
#
# Aurora names its SDL3 in cmake/AuroraDependencyVersions.cmake (AURORA_SDL3_REF,
# a release tag). ps5/patches/sdl.patch registers the console's video, audio and
# joystick drivers, which live in ps5/sdl/, and builds them when the toolchain
# says PS5.
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdl="$root/.deps/SDL"
versions="$root/.deps/aurora/cmake/AuroraDependencyVersions.cmake"
[[ -f $versions ]] || bash "$root/ps5/tools/setup-source.sh"
python3 -B "$root/ps5/tools/setup-submodules.py" --group sdl
python3 -B "$root/ps5/tools/prepare-source.py" --target sdl
echo "$sdl"
