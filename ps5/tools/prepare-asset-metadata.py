#!/usr/bin/env python3
"""Refresh disc-free metadata only when its pinned inputs change."""
import hashlib
from pathlib import Path
import subprocess
import tempfile


def input_key(upstream, decomp, region):
    digest = hashlib.sha256(region.encode())
    digest.update(subprocess.check_output(['git', '-C', str(decomp), 'rev-parse', 'HEAD']))
    for path in (Path(__file__), upstream / 'native/tools/gen_assets.sh',
                 upstream / 'native/tools/gen_asset_stubs.py', upstream / 'game/UPSTREAM'):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def prepare(upstream, decomp, output, region):
    key = input_key(upstream, decomp, region)
    stamp = output / '.ps5-metadata-key'
    if (output / 'include/assets/cos_assets.h').is_file() and stamp.is_file() and stamp.read_text() == key:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    # Generate beside the destination: failures retain the previous usable metadata.
    with tempfile.TemporaryDirectory(prefix='.metadata-', dir=output.parent) as folder:
        pending = Path(folder) / 'new'
        subprocess.run(['bash', str(upstream / 'native/tools/gen_assets.sh'), '--stubs',
                        '--version', region, '--decomp', str(decomp), '--out', str(pending)], check=True)
        if not (pending / 'include/assets/cos_assets.h').is_file():
            raise RuntimeError('Metadata generator did not produce cos_assets.h')
        (pending / stamp.name).write_text(key)
        previous = Path(folder) / 'previous'
        if output.exists():
            output.rename(previous)
        try:
            pending.rename(output)
        except OSError:
            if previous.exists():
                previous.rename(output)
            raise
    return True


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    for region in ('GZLE01', 'GZLP01'):
        prepare(root / 'vendor/switchwaker', root / '.deps/decomp', root / '.deps/assets' / region, region)
