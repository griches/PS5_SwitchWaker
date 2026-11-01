#!/usr/bin/env bash
# Wind Waker for the PlayStation 5 - Dawn at the revision Aurora pins, patched for the console.
#
#   ps5/tools/setup-dawn.sh      fetch it into .deps/dawn (once a revision), print the path
#
# Aurora (.deps/aurora, tools/setup-source.sh) names its Dawn in
# cmake/AuroraDependencyVersions.cmake (AURORA_DAWN_REF, a commit of
# encounter/dawn). This initializes its submodule and the nested Git submodules selected by
# tools/fetch_dawn_dependencies.py, then applies ps5/patches/dawn.patch: the
# PS5 as a platform, the RADV linked into the title in place of a Vulkan
# library, and the console's surface (VK_KHR_display). Dawn's Abseil gets
# ps5/patches/dawn-abseil.patch (its raw log writes with write(), not syscall()).
#
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dawn="$root/.deps/dawn"
versions="$root/.deps/aurora/cmake/AuroraDependencyVersions.cmake"
[[ -f $versions ]] || bash "$root/ps5/tools/setup-source.sh"
# Dawn and its selected nested modules use their existing Git pins. Never run
# its fetch helper here: that helper can check out populated dependency trees.
python3 -B "$root/ps5/tools/setup-submodules.py" --group dawn
python3 -B "$root/ps5/tools/prepare-source.py" --target dawn
python3 -B "$root/ps5/tools/prepare-source.py" --target dawn-abseil
echo "$dawn"
