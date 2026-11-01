#!/usr/bin/env python3
"""Validate controller sources, generated assets and packaging with the standard library."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('controller_ui', ROOT / 'ps5/tools/generate-controller-ui.py')
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)

def pane_records(data):
    records = {}
    at = 32
    while at + 8 <= len(data):
        kind, size = struct.unpack_from('>4sI', data, at)
        if size < 8 or at + size > len(data):
            raise ValueError('Malformed BLO block')
        if kind in (b'PAN1', b'PIC1', b'TBX1', b'WIN1'):
            tag = data[at + 12:at + 16].hex()
            key = kind.decode() + ':' + tag
            if key in records:
                raise ValueError('Ambiguous BLO pane tag')
            records[key] = {'bounds': list(struct.unpack_from('>hhhh', data, at + 16)),
                            'sha256': ui.sha(data[at:at + size])}
        at += size
        if kind == b'EXT1':
            break
    return records


def picture(block, resource=None, bounds=None, neutral=False):
    """Edit a stock PIC1 without borrowing any third-party layout bytes."""
    out = bytearray(block)
    if bounds is not None:
        struct.pack_into('>hhhh', out, 16, *bounds)
    # Pane stream: 6 mandatory fields, optional rotation/base/alpha/inheritance.
    count = out[8]
    end = 24 + (2 if count >= 7 else 0) + max(0, count - 7)
    end = (end + 3) & ~3
    if neutral and count >= 7:
        struct.pack_into('>H', out, 24, 0)  # Upright controller artwork.
    num = out[end]
    cursor = end + 1
    refs = []
    for _ in range(2):
        length = out[cursor + 1]
        refs.append(bytes(out[cursor:cursor + 2 + length]))
        cursor += 2 + length
    if resource is not None:
        encoded = resource.encode('ascii')
        refs[0] = bytes((refs[0][0], len(encoded))) + encoded
    binding = out[cursor]
    flags = out[cursor + 1] if num >= 4 else 0
    if neutral:
        # Preserve binding/mirroring but remove the GC colour multiplication.
        tail = bytes((11,)) + b''.join(refs) + bytes((binding, flags, 5))
        tail += bytes(4) + b'\xff' * 20
        out = out[:end] + tail
    else:
        out = out[:end + 1] + b''.join(refs) + out[cursor:]
    out.extend(bytes(-len(out) % 4))
    struct.pack_into('>I', out, 4, len(out))
    return bytes(out)


def apply_layout(stock, recipe):
    assert recipe[:8] == b'P5UILAY1'
    count = struct.unpack_from('>I', recipe, 8)[0]
    assert len(recipe) == 32 + count * 64
    rules = {recipe[at:at + 4]: recipe[at:at + 64]
             for at in range(32, len(recipe), 64)}
    out, at = bytearray(stock[:32]), 32
    while at + 8 <= len(stock):
        kind, size = struct.unpack_from('>4sI', stock, at)
        if size < 8 or at + size > len(stock):
            raise ValueError('Malformed stock layout')
        block = stock[at:at + size]
        if kind == b'PIC1':
            rule = rules.get(block[12:16])
            bounds = struct.unpack_from('>hhhh', rule, 4) if rule and rule[12] & 1 else None
            resource = rule[16:].split(b'\0')[0].decode() if rule and rule[12] & 2 else None
            neutral = resource is not None or any(n.encode() in block for n in ui.PROMPTS)
            block = picture(block, resource, bounds, neutral)
        out.extend(block)
        at += size
        if kind == b'EXT1':
            out.extend(stock[at:])
            struct.pack_into('>I', out, 8, len(out))
            return bytes(out)
    raise ValueError('Missing layout end')


# Exercise the authored rules using synthetic layouts, without storing disc data.
def synthetic_picture(tag, texture):
    block = bytearray(29)
    block[:4] = b'PIC1'
    block[8] = 7
    block[12:16] = tag.encode().rjust(4, b'\0')
    struct.pack_into('>hhhhH', block, 16, 1, 2, 3, 4, 90)
    block[28] = 4
    name = texture.encode()
    block.extend(bytes((2, len(name))) + name + bytes((0, 0, 7, 3)))
    block.extend(bytes(-len(block) % 4))
    struct.pack_into('>I', block, 4, len(block))
    return bytes(block)

for file_select, group in ((False, 'hud'), (True, 'file_select')):
    rules = json.loads((ui.ASSETS / 'layout.json').read_text())[group]
    source = bytearray(32)
    source[:8] = b'SCRNblo1'
    for tag in rules:
        source.extend(synthetic_picture(tag, 'untouched.bti'))
    untouched = synthetic_picture('keep', 'untouched.bti')
    source.extend(untouched)
    source.extend(b'EXT1' + struct.pack('>I', 8))
    struct.pack_into('>I', source, 8, len(source))
    original = bytes(source)
    result = apply_layout(original, ui.layout_recipe(file_select))
    panes = pane_records(result)
    for tag, bounds in rules.items():
        assert panes['PIC1:' + tag.encode().rjust(4, b'\0').hex()]['bounds'] == bounds
    assert untouched in result and bytes(source) == original
    assert (b'ba_base_04.bti' if file_select else b'font_02.bti') in result

with tempfile.TemporaryDirectory(prefix='ps5-controller-ui-') as temporary:
    output = ui.ASSETS
    originals = json.loads((ui.ASSETS / 'resources.json').read_text())
    rules = json.loads((ui.ASSETS / 'layout.json').read_text())
    for region, game_id in (('us', 'GZLE01'), ('pal', 'GZLP01')):
        folder = output / region
        data = (folder / 'resources.bin').read_bytes()
        manifest = json.loads((folder / 'manifest.json').read_text())
        decoded = ui.decode_bundle(data)
        assert manifest['game_id'] == game_id and ui.sha(data) == manifest['bundle_sha256']
        assert manifest['source'] == ui.SOURCE and manifest['author'] == 'Xelu'
        assert manifest['license'] == 'CC0-1.0'
        assert len(decoded) == len(ui.PROMPTS) + 6 == len(manifest['resources'])
        for item in manifest['resources']:
            volume, name = item['volume'], item['name']
            payload, size, crc = decoded[(volume, name)]
            if volume == 'prosperowake':
                assert name in ('camera_self.bti', 'zoom_arrow.bti') and size == crc == 0
                assert ui.sha(payload) == item['sha256']
                assert payload == (ui.camera_prompt() if name == 'camera_self.bti' else ui.zoom_arrow())
                continue
            original = originals[volume if volume == 'menures' else f'file_select_{region}']['resources'][name]
            assert (size, crc) == (original['size'], original['crc32'])
            assert len(payload) == item['size'] and ui.sha(payload) == item['sha256']
            if name.endswith('.bti'):
                assert ui.sha((ui.ASSETS / item['prompt']).read_bytes()) == item['prompt_sha256']
                assert payload == ui.texture(name)
            else:
                assert payload == ui.layout_recipe(volume == 'file_select')
                assert payload[:8] == b'P5UILAY1'
                assert b'SCRNblo1' not in payload
        key, (payload, _, _) = next((k, v) for k, v in decoded.items() if k[1].endswith('.bti'))
        metadata = {'size': 8, 'crc32': ui.crc(b'original')}
        bundle = ui.make_bundle([(key[0], key[1], metadata, payload)])
        assert ui.decode_bundle(bundle)[key] == (payload, 8, ui.crc(b'original'))
        for bad in (bundle[:-1], bundle[:20], bundle[:-1] + bytes([bundle[-1] ^ 1])):
            try:
                ui.decode_bundle(bad)
                raise AssertionError('Malformed bundle accepted')
            except ValueError:
                pass
    camera = decoded[('prosperowake', 'camera_self.bti')][0]
    assert not list(ui.ASSETS.glob('*.bti'))
    assert len(camera) == 32 + 128 * 32 * 2
    assert camera[0] == 5 and camera[8] == 0 and camera[24] == 1
    # No eye artwork in the generated texture: the right 72 pixels are empty.
    for y in range(32):
        for x in range(56, 128):
            at = 32 + ((y // 4) * 32 + x // 4) * 32 + ((y % 4) * 4 + x % 4) * 2
            assert camera[at:at + 2] == b'\0\0'
    assert struct.unpack_from('>HH', camera, 2) == (128, 32)
    assert struct.unpack_from('>I', camera, 28)[0] == 32
    assert (output / 'menu-prompts.rgba').stat().st_size == 600 * 100 * 4
    # Reproduce every runtime output in an empty folder, without reading old outputs.
    generated = Path(temporary) / 'generated'
    ui.generate(generated)
    for p in generated.rglob('*'):
        if p.is_file():
            assert p.read_bytes() == (output / p.relative_to(generated)).read_bytes()
    print('Dependency-free source regeneration passed.')

package = (ROOT / 'ps5/tools/package-title.sh').read_text()
assert 'generate-controller-ui.py' not in package
assert 'manifest.json' not in package
assert 'manifest.json' not in (ROOT / 'ps5/tools/deploy.sh').read_text()
assert 'manifest.json' not in (ROOT / 'ps5/cmake/link-inputs.cmake').read_text()
assert 'PIL' not in (ROOT / 'ps5/tools/build.sh').read_text()
assert 'cp --remove-destination "$source" "$app/res/$resource"' in package
assert not (ui.ASSETS / 'camera-eye.png').exists()
assert not (ui.ASSETS / 'stock').exists()
assert not list(ui.ASSETS.rglob('*.blo'))
used = {f'PS5_{n}.png' for n in ui.PROMPTS.values()} | {f'PS5_{n}.png' for n in ui.FOOTER} | {'PS5_Right_Stick_Click.png'}
assert {p.name for p in (ui.ASSETS / 'xelu').glob('*.png')} == used
credits = (ROOT / 'licenses/Controller-prompts.txt').read_text()
assert 'Xelu' in credits and 'CC0' in credits
menu = (ROOT / 'ps5/src/presentation/pc_menu.cpp').read_text()
overlay = menu.split('void menuOverlayFrame()', 1)[1].split('void menuFrame()', 1)[0]
assert 'if (m.open) welcomeDone = true;' in overlay
assert 'ImGui::GetTime() - welcomeStart' in overlay and 'handleInput' not in overlay
frame = (ROOT / 'vendor/switchwaker/native/src/pc/runtime/pc_frame.cpp').read_text()
assert frame.count('menuOverlayFrame();') == 2
assert 'menuOverlayFrame();' in frame.split('overlayFrame(0, false);', 1)[1].split('aurora_end_frame();', 1)[0]
deploy = (ROOT / 'ps5/tools/deploy.sh').read_text()
for name in ('resources-us.bin', 'resources-pal.bin', 'menu-prompts.rgba'):
    assert '--always res/' + name in deploy
print('Controller UI passed: Xelu prompts, disc layout recipes, reproducible bundles, packaging and toast render paths.')

# Known scanlines exercise every PNG filter, including Paeth and alpha values.
import zlib
rows = bytes([10, 20, 30, 255, 40, 50, 60, 128, 70, 80, 90, 64, 100, 110, 120, 0])
filtered = {
    0: (rows[:8], rows[8:]),
    1: (bytes([10, 20, 30, 255, 30, 30, 30, 129]), bytes([70, 80, 90, 64, 30, 30, 30, 192])),
    2: (rows[:8], bytes([60, 60, 60, 65, 60, 60, 60, 128])),
    3: (bytes([10, 20, 30, 255, 35, 40, 45, 1]), bytes([65, 70, 75, 193, 45, 45, 45, 160])),
    4: (bytes([10, 20, 30, 255, 30, 30, 30, 129]), bytes([60, 60, 60, 65, 30, 30, 30, 192])),
}
def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', ui.crc(kind + data))

with tempfile.TemporaryDirectory(prefix='ps5-png-') as temporary:
    png = Path(temporary) / 'filter.png'
    for method, (first, second) in filtered.items():
        data = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
        data += png_chunk(b'IDAT', zlib.compress(bytes([method]) + first + bytes([method]) + second))
        data += png_chunk(b'IEND', b'')
        png.write_bytes(data)
        image = ui.read_png(png)
        assert image.size == (2, 2) and image.pixels == rows, method
    png.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    try:
        ui.read_png(png)
        raise AssertionError('Corrupt PNG accepted')
    except ValueError:
        pass
# Transparent green must not create a dark/green fringe on opaque red.
image = ui.RgbaImage(2, 1, bytes([255, 0, 0, 255, 0, 255, 0, 0]))
assert image.resize((1, 1)).pixels == bytes([255, 0, 0, 128])
assert image.crop((1, 0, 2, 1)).pixels == bytes([0, 255, 0, 0])
print('Standard-library PNG filters, corruption handling and alpha-aware resizing passed.')

# Telescope and Picto Box zoom use upright left-stick prompts in both blend slots.
for filename, panes in (('d_scope.cpp', ('mCrtnPane', 'mCzomPane')),
                        ('d_picture_box.cpp', ('pane_crtn', 'pane_czom'))):
    source = (ROOT / 'vendor/switchwaker/game/src/d' / filename).read_text()
    zoom = source.split('// Zoom uses the left stick.', 1)[1].split('#else', 1)[0]
    assert all(pane in zoom for pane in panes)
    assert 'changeTexture("font_07_01.bti", 0)' in zoom
    assert 'fopMsgM_blendInit(pane, "font_07_02.bti")' in zoom
    assert 'ROTATE_Z, 0.0f' in zoom and 'font_09' not in zoom
    assert 'setBlackWhite(JUtility::TColor(0), JUtility::TColor(0xffffffff))' in zoom
    assert 'setCornerColor(JUtility::TColor(0xffffffff))' in zoom
print('Zoom prompts passed: left stick, upright, telescope and Picto Box.')

# Both animation slots use the generated arrow without flattening its border.
for filename in ('d_scope.cpp', 'd_picture_box.cpp'):
    source = (ROOT / 'vendor/switchwaker/game/src/d' / filename).read_text()
    arrows = source.split('// Use the generated arrow', 1)[1].split('#endif', 1)[0]
    assert 'picture->changeTexture(arrow, 0)' in arrows
    assert 'picture->changeTexture(arrow, 1)' in arrows
    assert 'setBlackWhite(JUtility::TColor(0), JUtility::TColor(0xffffffff))' in arrows
    assert 'setCornerColor(JUtility::TColor(0xffffffff))' in arrows
    assert '180.0f : 0.0f' in arrows
    assert 'paneTrans(' not in arrows and 'setBlendRatio(' not in arrows
print('Zoom arrow styling passed: generated artwork, neutral colours, both animation slots.')

# The packaged RGB5A3 arrow must retain a light face, dark rim and soft alpha edge.
arrow = ui.zoom_arrow()
values = [int.from_bytes(arrow[i:i + 2], 'big') for i in range(32, len(arrow), 2)]
assert any(v & 0x8000 and (v & 31) >= 27 for v in values)
assert any(v & 0x8000 and 4 <= (v & 31) <= 10 for v in values)
assert any(not v & 0x8000 and 0 < (v >> 12) < 7 for v in values)
