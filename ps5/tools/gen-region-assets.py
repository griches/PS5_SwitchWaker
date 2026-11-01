#!/usr/bin/env python3
"""Combine US/PAL stub metadata into one native asset interface; contains no disc data.

Consumes the ordinary upstream --stubs outputs. PAL's registration order is the
canonical order; US rows are matched by header, never by their regional numbers.
Only changed files are written, so repeated setup does not invalidate objects.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re


def write(path, text):
    data = text.encode() if isinstance(text, str) else text
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def load(root):
    assets = root / 'include/assets'
    index = json.loads((assets / 'cos_asset_index.json').read_text())
    rows = [line for line in (assets / 'cos_asset_table.inc').read_text().splitlines()
            if line.startswith('COS_ASSET_ROW(')]
    if [item['id'] for item in index] != list(range(len(rows))):
        raise ValueError('Asset registration IDs must be contiguous and match the table')
    for item, row in zip(index, rows):
        fields = next(csv.reader([row.removeprefix('COS_ASSET_ROW(').removesuffix(')')], skipinitialspace=True))
        if len(fields) != 8 or fields[0] != item['name'] or int(fields[5], 0) != item['size'] or fields[6] != item['kind']:
            raise ValueError('Asset index and registration table disagree')
    result = {item['header']: (item, rows[item['id']]) for item in index}
    if len(result) != len(index):
        raise ValueError('Duplicate asset header identity')
    return result


def resources(us, pal):
    """Retain constant enums; only region-varying names get runtime expressions."""
    pattern = r'\b(dRes_\w+_e)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)'
    a = dict(re.findall(pattern, us))
    b = dict(re.findall(pattern, pal))
    extra = []
    for name in sorted(a.keys() & b.keys()):
        if int(a[name], 0) != int(b[name], 0):
            # Preserve safe initialization of upstream u8/u16 resource tables.
            # A plain dynamic int expression would be a narrowing list initializer.
            width = max(int(a[name], 0), int(b[name], 0))
            scalar = 'unsigned char' if width <= 255 else 'unsigned short' if width <= 65535 else 'unsigned int'
            extra.append(f'#define {name} static_cast<{scalar}>(pc_disc_is_pal() ? {b[name]} : {a[name]})')
    # US-only entries remain available to runtime US branches; no translated
    # resource is synthesized for a disc that does not contain it.
    for name in sorted(a.keys() - b.keys()):
        extra.append(f'inline constexpr int {name} = {a[name]};')
    if not extra:
        return pal
    end = pal.rfind('#endif')
    if end < 0:
        raise ValueError('Resource header has no include guard')
    return pal[:end] + '#include "pc/pc_disc_region.h"\n' + '\n'.join(extra) + '\n\n' + pal[end:]


def generate(us, pal, out):
    a, b = load(us), load(pal)
    if a.keys() - b.keys():
        raise ValueError('PAL canonical table no longer contains all shared US assets')
    us_rows = []
    for header, (item, _) in b.items():
        if header in a:
            other, row = a[header]
            if any(item[key] != other[key] for key in ('name', 'kind', 'size')):
                raise ValueError(f'Incompatible shared asset: {header}')
            us_rows.append(row)
        else:
            if header not in ('msg_data_ge.h', 'msg_data_fr.h', 'msg_data_sp.h', 'msg_data_it.h'):
                raise ValueError(f'Unreviewed PAL-only asset: {header}')
            # Unused language arrays on US are explicitly absent, not read from guessed offsets.
            us_rows.append(f'COS_ASSET_ROW("{item["name"]}", "", "", 0, 0, {item["size"]}, {item["kind"]}, "")')
    for source in (pal / 'include/assets').iterdir():
        if source.is_file():
            write(out / 'include/assets' / source.name, source.read_bytes())
    write(out / 'include/assets/cos_asset_table_us.inc', '\n'.join(us_rows) + '\n')
    write(out / 'include/assets/cos_asset_table_pal.inc',
          '\n'.join(row for _, row in b.values()) + '\n')
    # Region-specific paths may coexist; common paths retain all symbolic names.
    for source in (us / 'res').rglob('*.h'):
        relative = source.relative_to(us)
        peer = pal / relative
        text = resources(source.read_text(), peer.read_text()) if peer.exists() else source.read_text()
        write(out / relative, text)
    for source in (pal / 'res').rglob('*.h'):
        relative = source.relative_to(pal)
        if not (us / relative).exists():
            write(out / relative, source.read_bytes())
    write(out / 'regions.json', json.dumps({'regions': ['GZLE01', 'GZLP01'],
          'canonical': 'GZLP01', 'arrays': len(b)}, indent=2) + '\n')


def prepare(us, pal, out):
    # setup-source owns these inputs; its stamps change with decomp/generator pins.
    # Avoid rereading hundreds of generated headers across the Lima shared mount.
    inputs = [us / '.ps5-metadata-key', pal / '.ps5-metadata-key']
    key = None
    stamp = out / '.ps5-region-key'
    if all(path.is_file() for path in inputs):
        key = hashlib.sha256(Path(__file__).read_bytes() + b'\0' +
            b'\0'.join(path.read_bytes() for path in inputs)).hexdigest()
        required = ('regions.json', 'include/assets/cos_assets.h',
                    'include/assets/cos_asset_table_us.inc', 'include/assets/cos_asset_table_pal.inc')
        if stamp.is_file() and stamp.read_text() == key and all((out / name).is_file() for name in required):
            return False
    generate(us, pal, out)
    if key is not None:
        write(stamp, key)
    elif stamp.exists():
        stamp.unlink()
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--us', required=True, type=Path)
    parser.add_argument('--pal', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    prepare(args.us, args.pal, args.out)


if __name__ == '__main__':
    main()
