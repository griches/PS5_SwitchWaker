#!/usr/bin/env bash
# Stage a complete title before publishing it. No compilation or deployment.
# SPDX-License-Identifier: MIT
set -euo pipefail
work=$1 vulkan=$2 sdk=$3 archive=$4
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
tool="$vulkan/build/host/ps5-native-tool"
param="$work/param.json"
# The SDK supplies nm; objdump may live beside the selected host Clang instead.
nm=${PS5_LLVM_NM:-$sdk/bin/llvm-nm}
objdump=${PS5_LLVM_OBJDUMP:-$sdk/bin/llvm-objdump}
if [[ ! -x $objdump && -z ${PS5_LLVM_OBJDUMP:-} ]]; then
    compiler=$(command -v "${PS5_CLANG:-clang}" || true)
    if [[ -n $compiler ]]; then
        compiler=$(readlink -f "$compiler")
        objdump="$(dirname "$compiler")/llvm-objdump"
    fi
    [[ -x $objdump ]] || objdump=$(command -v llvm-objdump || true)
fi
[[ -x $nm && -x $objdump ]] || {
    echo "missing LLVM analysis tools; set PS5_LLVM_NM and PS5_LLVM_OBJDUMP to executable paths" >&2
    exit 2
}
# The title folder
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
mkdir -p "$root/dist"
published="$root/dist/$title_id"
staging=$(mktemp -d "$root/dist/.${title_id}.stage.XXXXXX")
app="$staging/title"
mkdir -p "$app"
backup="$staging/previous"
cleanup() {
    if [[ -d $backup && ! -e $published ]]; then
        mv "$backup" "$published" || { echo "restore failed; previous title retained at $backup" >&2; return 1; }
    fi
    rm -rf -- "$staging"
}
trap cleanup EXIT
# Same-filesystem hardlinks preserve large user files without copying their bytes.
# Generated outputs must be unlinked/replaced, never modified through these links.
[[ ! -d $published ]] || cp -al "$published/." "$app/"
rm -f "$app/eboot.bin" "$app/BUILD_INPUTS.json" "$app/res/div-table.bin" "$app/SOURCE.txt"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp --remove-destination "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $ps5/sce_sys/$asset ]] && cp --remove-destination "$ps5/sce_sys/$asset" "$app/sce_sys/$asset"
done
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp --remove-destination "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
# The list of render pipelines the game uses, compiled while it starts: pipeline
# keys recorded from runs of the game, no game data (docs/SWITCH_BUILD.md)
upstream="$root/vendor/switchwaker"
cp --remove-destination "$upstream/native/data/initial_pipeline_cache.db" "$app/initial_pipeline_cache.db"
# Controller UI: copy ready-to-package assets. Regeneration is maintenance only.
rm -rf "$app/res/ps5-glyphs" "$app/res/controller-ui" "$app/res/shader-cache"
mkdir -p "$app/res"
for entry in us/resources.bin:resources-us.bin pal/resources.bin:resources-pal.bin menu-prompts.rgba:menu-prompts.rgba; do
    source="$ps5/assets/controller-ui/${entry%:*}"
    resource=${entry#*:}
    [[ -f $source ]] || { echo "missing bundled controller asset: $source" >&2; exit 2; }
    cp --remove-destination "$source" "$app/res/$resource"
done
# The shader caches a console filled on a first start (ps5/shader-cache: Dawn's
# cache and the driver's), copied into place by a start that finds none
# (ps5/src/startup.c): every console has the same GPU and this title's driver, so
# the first start need not compile three thousand pipelines again
if [[ -f $ps5/shader-cache/dawn_cache.db ]]; then
    mkdir -p "$app/res/shader-cache"
    cp -R "$ps5/shader-cache/." "$app/res/shader-cache/"
fi
# The licences beside eboot.bin
mkdir -p "$app/licenses"
cp --remove-destination "$upstream/LICENSE" "$app/licenses/SwitchWaker-LICENSE.txt"
cp --remove-destination "$upstream/game/LICENSE" "$app/licenses/SwitchWaker-game-LICENSE.txt"
cp --remove-destination "$upstream/native/dsp_hle/LICENSE" "$app/licenses/Dolphin-DSPHLE-LICENSE.txt"
cp --remove-destination "$upstream/THIRD_PARTY.md" "$app/licenses/SwitchWaker-THIRD_PARTY.md"
cp --remove-destination "$root/.deps/aurora/LICENSE" "$app/licenses/Aurora-LICENSE.txt"
cp --remove-destination "$vulkan/LICENSE" "$app/licenses/PS5_Vulkan-LICENSE.txt"
cp --remove-destination "$root/licenses/LLVM-compiler-rt.txt" "$app/licenses/LLVM-compiler-rt.txt"
cp --remove-destination "$root/licenses/Controller-prompts.txt" "$app/licenses/Controller-prompts.txt"
# The title's integer division instructions, for the SIGFPE handler that finishes a division by
# zero as the GameCube and ARM do (the console's code cannot be read at run time)
mkdir -p "$app/res"
python3 "$ps5/tools/make-div-table.py" "$work/link/llvm-pie.elf" "$app/res/div-table.bin" \
    --nm "$nm" --objdump "$objdump"
python3 -B "$ps5/tools/source-provenance.py" --output "$app/BUILD_INPUTS.json" \
    --vulkan "$vulkan" --archive "$archive" --sdk "$sdk"
# Prepare debug artifacts before publishing the staged title.
mkdir -p "$root/build"
cp --remove-destination "$work/link/llvm-pie.elf" "$root/build/last-title.elf"
# Rename on the same filesystem. A failed publication restores the previous title.
[[ ! -e $published ]] || mv "$published" "$backup"
mv "$app" "$published"
# Publish the build-system output only once all packaging work succeeded.
mv "$work/eboot.elf.new" "$work/eboot.elf"
printf '==> %s: %s\n' "$title_id" "$published"
