#!/usr/bin/env python3
"""Runtime-region metadata fixtures; no compiler or game data required."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('region_assets', ROOT / 'ps5/tools/gen-region-assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


def fixture(root, names):
    folder = root / 'include/assets'
    folder.mkdir(parents=True)
    items = [dict(id=i, header=name + '.h', name=name, kind='COS_ASSET_RAW', size=16)
             for i, name in enumerate(names)]
    (folder / 'cos_asset_index.json').write_text(json.dumps(items))
    (folder / 'cos_asset_table.inc').write_text(''.join(
        f'COS_ASSET_ROW("{name}", "{root.name}/{name}", "", 0, {i}, 16, COS_ASSET_RAW, "hash")\n'
        for i, name in enumerate(names)))
    (root / 'res').mkdir()
    return items


class RegionAssets(unittest.TestCase):
    def test_reorders_by_identity_and_preserves_absent_languages(self):
        with tempfile.TemporaryDirectory() as folder:
            us, pal, out = [Path(folder) / name for name in ('us', 'pal', 'out')]
            fixture(us, ['first', 'second'])
            fixture(pal, ['second', 'msg_data_fr', 'first'])
            assets.generate(us, pal, out)
            rows = (out / 'include/assets/cos_asset_table_us.inc').read_text().splitlines()
            self.assertIn('"second", "us/second"', rows[0])
            self.assertIn('"msg_data_fr", "", "", 0, 0, 16', rows[1])
            self.assertIn('"first", "us/first"', rows[2])
            before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in out.rglob('*') if p.is_file()}
            assets.generate(us, pal, out)
            self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in before})

    def test_preparation_reuses_stamped_inputs_and_retries_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            us, pal, out = [Path(folder) / name for name in ('us', 'pal', 'out')]
            for root in (us, pal):
                fixture(root, ['first'])
                (root / '.ps5-metadata-key').write_text('pin-a')
                (root / 'include/assets/cos_assets.h').write_text('fixture header')
            self.assertTrue(assets.prepare(us, pal, out))
            before = {p: p.stat().st_mtime_ns for p in out.rglob('*') if p.is_file()}
            with patch.object(assets, 'generate', side_effect=AssertionError('unexpected regeneration')):
                self.assertFalse(assets.prepare(us, pal, out))
            self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})
            (us / '.ps5-metadata-key').write_text('pin-b')
            with patch.object(assets, 'generate', side_effect=ValueError('bad inputs')):
                with self.assertRaises(ValueError):
                    assets.prepare(us, pal, out)
            self.assertTrue(assets.prepare(us, pal, out))
            (out / 'include/assets/cos_asset_table_us.inc').unlink()
            self.assertTrue(assets.prepare(us, pal, out))

    def test_rejects_incompatible_or_unreviewed_assets(self):
        with tempfile.TemporaryDirectory() as folder:
            us, pal, out = [Path(folder) / name for name in ('us', 'pal', 'out')]
            fixture(us, ['first'])
            fixture(pal, ['first', 'unknown_language'])
            with self.assertRaisesRegex(ValueError, 'Unreviewed'):
                assets.generate(us, pal, out)
            index = pal / 'include/assets/cos_asset_index.json'
            data = json.loads(index.read_text())
            data[0]['size'] = 32
            index.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'disagree'):
                assets.generate(us, pal, out)

    def test_resources_keep_constants_and_safe_runtime_widths(self):
        def header(body):
            return '#ifndef RES_TEST_H\n#define RES_TEST_H\nenum Index {\n' + body + '\n};\n#endif\n'
        us = header('dRes_A_e=1, dRes_B_e=3, dRes_US_e=4, dRes_BIG_e=300,')
        pal = header('dRes_A_e=1, dRes_B_e=2, dRes_PAL_e=5, dRes_BIG_e=301,')
        merged = assets.resources(us, pal)
        self.assertNotIn('#define dRes_A_e', merged)
        self.assertIn('static_cast<unsigned char>(pc_disc_is_pal() ? 2 : 3)', merged)
        self.assertIn('static_cast<unsigned short>(pc_disc_is_pal() ? 301 : 300)', merged)
        self.assertIn('inline constexpr int dRes_US_e = 4;', merged)
        self.assertIn('dRes_PAL_e=5', merged)


if __name__ == '__main__':
    unittest.main()
