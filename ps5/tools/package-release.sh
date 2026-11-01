#!/usr/bin/env bash
# Wind Waker for the PlayStation 5 - pack the built title folder as a release archive.
#
#   ps5/tools/package-release.sh
#
# Writes dist/ProsperoWake-build-N.zip: the title folder dist/<TITLE_ID>/ as the
# console takes it (eboot.bin, sce_sys/, sce_module/, res/, licenses/), with
# SOURCE.txt beside eboot.bin naming the revision of every part the build
# linked. A built title is under GPL-3.0: that file is how a release says where
# its source is. No disc image, save or texture pack goes in, and it refuses if
# one is in the folder.
#
# After PS5_Dusklight's package-release.sh.
# SPDX-License-Identifier: MIT
set -euo pipefail
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
[[ $# == 0 ]] || { echo "usage: package-release.sh (uses the packaged build number)" >&2; exit 2; }
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$ps5/sce_sys/param.json")
app="$root/dist/$title_id"
for file in BUILD_INPUTS.json eboot.bin initial_pipeline_cache.db sce_sys/param.json sce_sys/icon0.png sce_sys/pic0.dds \
    sce_sys/pic1.dds sce_module/libc.prx res/shader-cache/dawn_cache.db; do
    [[ -e $app/$file ]] || { echo "missing $app/$file: run build.sh first" >&2; exit 2; }
done
# the caches the title ships are under res/; a console's own, a save or a pack are not for a release
stray=$(find "$app" \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.gcm' -o -iname '*.rvz' -o -iname '*.gci' \
    -o -path "$app/user" -o -path "$app/radv-shader-cache" -o -name hd_textures \) -print -quit)
[[ -z $stray ]] || { echo "game data or a save in the title folder ($stray): a release never carries it" >&2; exit 2; }
# the submodule's working tree carries the console's patches: that is not a change here
[[ -z $(git -C "$root" status --porcelain --ignore-submodules=dirty) ]] ||
    { echo "uncommitted changes: a release is built from a commit" >&2; exit 2; }
mkdir -p "$app/licenses"
cp "$root/licenses/GPL-3.0.txt" "$root/licenses/MIT.txt" "$root/licenses/Zlib.txt" "$app/licenses/"
cp "$root/LICENSING.md" "$app/licenses/LICENSING.md"
# Describe the packaged binary, not whichever checkout happens to be current.
build_number=$(python3 - "$app" <<'PYTHON'
import hashlib
import json
import re
from pathlib import Path
import sys

app = Path(sys.argv[1])
metadata = json.loads((app / 'sce_sys/param.json').read_text())
version = metadata.get('contentVersion', '')
if not isinstance(version, str) or not re.fullmatch(r'[0-9]{2}\.[0-9]{3}\.[0-9]{3}', version):
    raise SystemExit('Invalid packaged contentVersion; rebuild before packaging')
_, minor, patch = map(int, version.split('.'))
build_number = minor * 1000 + patch
record = json.loads((app / 'BUILD_INPUTS.json').read_text())
with (app / 'eboot.bin').open('rb') as binary:
    binary_hash = hashlib.file_digest(binary, 'sha256').hexdigest()
if record.get('eboot_sha256') != binary_hash:
    raise SystemExit('Build metadata is missing or does not match eboot.bin; rebuild before packaging')
if record.get('port_tracked_worktree_dirty', True):
    raise SystemExit('This executable was built with uncommitted source changes; commit and rebuild before releasing')
lines = [f'ProsperoWake, Build {build_number}',
         'Unofficial PS5 homebrew. Not affiliated with Nintendo or Sony Interactive Entertainment.',
         '', 'Distributed under GPL-3.0; see licenses/LICENSING.md and licenses/GPL-3.0.txt.',
         '', 'Source revisions recorded when this executable was packaged:',
         f"ProsperoWake: {record['port_revision']}"]
for name, dependency in sorted(record['dependencies'].items()):
    lines.append(f"{name}: {dependency['revision']}")
external = record['external']
lines += [f"PS5_Vulkan: {external['vulkan_revision']}",
          f"PS5_PayloadSDK: {external['sdk_revision']}",
          f"RADV archive SHA-256: {external['radv_archive_sha256']}"]
if external.get('radv_provenance'):
    lines += ['', 'RADV build provenance:', external['radv_provenance'].rstrip()]
lines += ['', 'BUILD_INPUTS.json records the source/patch hashes and the extent of the dependency audit.',
          'No disc image, save or HD texture pack is included. Supply a US or PAL disc image.',
          '', f'eboot.bin SHA-256: {binary_hash}', '']
(app / 'SOURCE.txt').write_text('\n'.join(lines))
# Check the actual package, including external binaries and copied provenance.
# Never patch strings inside an already signed executable: rebuild with path maps.
private_path = re.compile(
    rb'(?:/(?:Users|home)/[^/\s\x00"\x27]+/|[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/][^\\/\s\x00"\x27]+[\\/])')
for path in sorted(app.rglob('*')):
    if path.is_file() and private_path.search(path.read_bytes()):
        raise SystemExit(f'Local build path in {path.relative_to(app)}; rebuild with source path remapping before releasing')
print(build_number)
PYTHON
)
out="$root/dist/ProsperoWake-build-$build_number.zip"
rm -f "$out"
(cd "$root/dist" && zip -q -r -X "$out" "$title_id" -x '*/.DS_Store')
printf '==> %s (%s bytes)\n' "$out" "$(stat -c %s "$out")"
