#!/usr/bin/env python3
"""Metadata invalidation/rollback fixtures; generator and Git calls are mocked."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metadata', ROOT / 'ps5/tools/prepare-asset-metadata.py')
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    upstream, decomp, output = root / 'upstream', root / 'decomp', root / 'assets'
    for name in ('native/tools/gen_assets.sh', 'native/tools/gen_asset_stubs.py', 'game/UPSTREAM'):
        path = upstream / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('fixture')
    calls = []
    def generate(args, **kwargs):
        calls.append(args)
        assert '--stubs' in args
        target = Path(args[args.index('--out') + 1]) / 'include/assets/cos_assets.h'
        target.parent.mkdir(parents=True)
        target.write_text('generation ' + str(len(calls)))
    with patch.object(metadata.subprocess, 'check_output', return_value=b'pin-a') as pin, \
         patch.object(metadata.subprocess, 'run', side_effect=generate) as generator:
        for region in ('GZLE01', 'GZLP01'):
            out = output / region
            assert metadata.prepare(upstream, decomp, out, region)
            stamp = out / '.ps5-metadata-key'
            before = stamp.stat().st_mtime_ns
            assert not metadata.prepare(upstream, decomp, out, region)
            assert stamp.stat().st_mtime_ns == before
        assert len(calls) == 2
        # Changed generators and decomp pins invalidate existing headers.
        (upstream / 'native/tools/gen_asset_stubs.py').write_text('changed generator')
        assert metadata.prepare(upstream, decomp, output / 'GZLE01', 'GZLE01')
        pin.return_value = b'pin-b'
        assert metadata.prepare(upstream, decomp, output / 'GZLE01', 'GZLE01')
        out = output / 'GZLE01'
        old = {p.relative_to(out): p.read_bytes() for p in out.rglob('*') if p.is_file()}
        pin.return_value = b'pin-c'
        generator.side_effect = subprocess.CalledProcessError(1, 'fixture')
        try:
            metadata.prepare(upstream, decomp, out, 'GZLE01')
            raise AssertionError('failed generator accepted')
        except subprocess.CalledProcessError:
            pass
        assert old == {p.relative_to(out): p.read_bytes() for p in out.rglob('*') if p.is_file()}
        generator.side_effect = generate
        assert metadata.prepare(upstream, decomp, out, 'GZLE01'), 'failed refresh must be retried'
print('Metadata checks passed: both regions, no-op reuse, generator/pin invalidation, failure retention and retry.')
