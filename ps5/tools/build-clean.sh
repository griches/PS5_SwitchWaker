#!/usr/bin/env bash
# Remove generated build/package/SDK files. Run after the build has stopped.
# Source submodules, bundled assets and player data are preserved.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
[[ $# == 0 ]] || { echo "Usage: bash ps5/tools/build-clean.sh" >&2; exit 2; }
if ps -eo comm= | grep -Eq '(^|/)(ninja|cmake|make|gmake|clang(-[0-9]+)?|clang\+\+|ld.lld)$'; then
    echo "Build tools are running. Stop the build before cleaning." >&2
    exit 1
fi
paths=("$root/build" "$root/dist" "$root/.deps/assets" "$root/.deps/native"
       "$root/.deps/controller-ui" "$root/.deps/controller-ui-pal" "$root/.deps/controller-ui-reference")
# Include the configured build and the established Lima cache, if present.
for path in "${PS5_BUILD_DIR:-$root/build/game}" "$HOME/.cache/prosperowake-build"; do
    [[ -d $path ]] || continue
    path=$(cd -- "$path" && pwd -P)
    case "$path" in "$root/build"|"$root/build/"*) continue ;; esac
    owner=$(sed -n 's/^PS5_PORT_DIR:[^=]*=//p' "$path/CMakeCache.txt" 2>/dev/null || true)
    [[ $owner == "$root" && $path != "$root" && $root != "$path/"* && $path != "$HOME" ]] || {
        echo "Refusing unrecognized build directory: $path" >&2; exit 1;
    }
    paths+=("$path")
done
# Check every destination before deleting anything.
for path in "${paths[@]}"; do
    [[ ! -L $path && $(realpath -m -- "$path") == "$path" ]] || {
        echo "Refusing redirected path: $path" >&2; exit 1;
    }
    [[ ! -e $path/.git ]] || { echo "Refusing source checkout: $path" >&2; exit 1; }
    case "$path" in "$root/"*)
        [[ -z $(git -C "$root" ls-files -- "$path") ]] || {
            echo "Refusing tracked files: $path" >&2; exit 1;
        } ;;
    esac
done
# The SDK installer uses the same lock.
if [[ -f $root/.deps/native/.ps5-sdk.lock ]]; then
    exec 9>"$root/.deps/native/.ps5-sdk.lock"
    flock -n 9 || { echo "SDK setup is running." >&2; exit 1; }
fi
for path in "${paths[@]}"; do
    [[ -e $path ]] || continue
    personal=$(find "$path" \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.gcm' -o -iname '*.rvz' \
        -o -iname '*.gci' -o -iname '*.sav' -o -iname '*.raw' -o -name user -o -name saves \
        -o -name hd_textures -o -name gamedata -o -name settings.ini -o -name imgui.ini \) -print -quit)
    if [[ -n $personal ]]; then
        echo "Keeping $path (contains player data: $personal)"
        continue
    fi
    echo "Removing $path"
    rm -rf -- "$path"
done
find "$root/ps5/tools" "$root/ps5/tests" -type d -name __pycache__ -prune -exec rm -rf -- {} +
rm -f -- "$root/.DS_Store"
echo "Clean complete. Run bash ps5/tools/build.sh to rebuild."
