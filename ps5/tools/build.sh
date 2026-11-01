#!/usr/bin/env bash
# Wind Waker for the PlayStation 5 - build the game into dist/<TITLE_ID>/.
#
#   ps5/tools/build.sh [ninja arguments]
#
# Fetches and patches the sources (tools/setup-source.sh), then configures
# SwitchWaker's own build (vendor/switchwaker/native) with the payload SDK's
# CMake toolchain and ps5/cmake/ps5-game.cmake, which gives it Dawn, SDL3
# and nod for the console and the title's link (tools/link-title.sh). The build
# reads no disc: COS_RUNTIME_ASSETS, with the stub headers of .deps/assets.
#
#   PS5_BUILD_DIR    the build tree (default build/game; the Linux VM's own disk
#                    is several times faster than the shared folder)
#   PS5_VULKAN_DIR   the PS5_Vulkan checkout (default ../PS5_Vulkan)
#
# SPDX-License-Identifier: MIT
set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
# One universal executable; remaining arguments are passed to Ninja.
case ${1:-} in
    auto|us|pal|--region|--region=*)
        echo "Region build modes were removed. Run build.sh [ninja arguments]; one executable supports US and PAL." >&2
        exit 2 ;;
esac
build=${PS5_BUILD_DIR:-$root/build/game}
mkdir -p "$build"
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}" && pwd)
# SDK wrappers select LLVM through llvm-config; keep them with the CRT compiler.
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
if [[ -z ${LLVM_CONFIG:-} && -n $PS5_CLANG ]]; then
    compiler=$(command -v "$PS5_CLANG" || true)
    if [[ -n $compiler ]]; then
        compiler=$(readlink -f "$compiler")
        llvm_config="$(dirname -- "$compiler")/llvm-config"
        [[ ! -x $llvm_config ]] || export LLVM_CONFIG="$llvm_config"
    fi
fi
bash "$ps5/tools/setup-source.sh"
dawn=$(bash "$ps5/tools/setup-dawn.sh")
sdl=$(bash "$ps5/tools/setup-sdl.sh")
sdk=$(bash "$ps5/tools/setup-sdk.sh")
ninja=$(command -v ninja || echo "$HOME/.local/bin/ninja")
echo "==> Universal US/PAL executable; build tree: $build"
# Reserve a number for this build attempt. CMake and packaging read the same
# metadata, so the menu and installed title cannot acquire different versions.
case " $* " in
    *" -n "*|*" -t "*|*" --help "*|*" --version "*) ;;
    *) python3 - "$ps5/sce_sys/param.json" "$build/param.json" <<'PY'
import json
import os
from pathlib import Path
import sys
import tempfile

template = Path(sys.argv[1])
path = Path(sys.argv[2])
metadata = json.loads(template.read_text())
major, minor, patch = map(int, metadata['contentVersion'].split('.'))
number = minor * 1000 + patch
if path.exists():
    previous = json.loads(path.read_text())
    prev_major, prev_minor, prev_patch = map(int, previous['contentVersion'].split('.'))
    if prev_major == major:
        number = max(number, prev_minor * 1000 + prev_patch)
number += 1
if number > 999999:
    raise SystemExit('Package build number exhausted')
metadata['contentVersion'] = f'{major:02d}.{number // 1000:03d}.{number % 1000:03d}'
with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as output:
    temporary = Path(output.name)
    json.dump(metadata, output, indent=2)
    output.write('\n')
try:
    temporary.chmod(template.stat().st_mode & 0o777)
    os.replace(temporary, path)
finally:
    temporary.unlink(missing_ok=True)
print(f'==> Build {number}')
PY
    ;;
esac
# Ninja handles ordinary CMake-file/source changes. Reconfigure explicitly only
# when invocation inputs change (or on request), rather than on every recompile.
configure_args=(
    -S "$root/vendor/switchwaker/native" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja"
    -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake"
    -DCMAKE_PROJECT_cos_native_INCLUDE="$ps5/cmake/ps5-game.cmake"
    -DPS5_PORT_DIR="$root" -DPS5_VULKAN_DIR="$vulkan" -DDAWN_DIR="$dawn" -DSDL_DIR="$sdl"
    -DFETCHCONTENT_SOURCE_DIR_AURORA="$root/.deps/aurora" -DCOS_AURORA_PREPATCHED=ON
    -DCOS_RECOMPCORE_DIR="$root/.deps/recompcore"
    -DCOS_RUNTIME_ASSETS=ON -DCOS_ASSETS_DIR="$root/.deps/assets/universal"
    -DCOS_VERSION=3 -DCOS_GAME_ID=GZLP01 -DCOS_RUNTIME_REGION=ON
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF
)
configure_key=$({
    printf '%s\0' "${configure_args[@]}" "$PS5_CLANG" "${LLVM_CONFIG:-}" "${RADV_ARCHIVE:-}"
    sha256sum "$ps5/tools/build.sh"
} | sha256sum | cut -d' ' -f1)
configure_state="$build/.ps5-configure-key"
if [[ ! -f $build/build.ninja || ! -f $build/CMakeCache.txt ||
      ! -f $configure_state || $(<"$configure_state") != "$configure_key" || ${PS5_RECONFIGURE:-0} == 1 ]]; then
    echo "==> Configuring changed build inputs"
    cmake "${configure_args[@]}" > "$build/configure.log" 2>&1 \
        || { tail -60 "$build/configure.log" >&2; exit 1; }
    printf '%s\n' "$configure_key" > "$configure_state"
fi

# The executable is EXCLUDE_FROM_ALL upstream: options alone must still build it.
case " $* " in *" switchwaker "*) ;; *) set -- "$@" switchwaker ;; esac
"$ninja" -C "$build" "$@"
