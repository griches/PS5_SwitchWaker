#!/usr/bin/env python3
"""Generate all controller UI outputs from Xelu PNGs, layout rules and resource checksums.

Uses only the Python standard library. Builds copy the committed outputs;
regeneration needs no image library, disc, source ZIP or prior output.
"""
import argparse
import hashlib
import json
import math
from functools import lru_cache
from pathlib import Path
import struct
import zlib

ASSETS = Path(__file__).resolve().parents[1] / 'assets/controller-ui'
SOURCE = 'https://thoseawesomeguys.com/prompts/'

def sha(data):
    return hashlib.sha256(data).hexdigest()


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def make_bundle(resources):
    # Fixed-width, endian-explicit index; every resource starts at a 32-byte boundary.
    header = bytearray(32)
    header[:8] = b'P5UIRES3'
    struct.pack_into('>I', header, 8, len(resources))
    index, payload = bytearray(), bytearray()
    data_start = 32 + 128 * len(resources)
    for volume, name, original, replacement in resources:
        if len(volume) >= 16 or len(name) >= 48:
            raise ValueError('UI resource name exceeds bundle limits')
        payload.extend(b'\0' * (-len(payload) % 32))
        record = bytearray(128)
        record[:len(volume)] = volume.encode()
        record[16:16 + len(name)] = name.encode()
        kind = b'TIMG' if name.endswith('.bti') else b'SCRN'
        struct.pack_into('>4sIIIII', record, 64, kind, data_start + len(payload),
                         len(replacement), crc(replacement), original["size"], original["crc32"])
        index.extend(record)
        payload.extend(replacement)
    result = header + index + payload
    struct.pack_into('>II', result, 12, len(result), crc(result[32:]))
    return bytes(result)


def decode_bundle(data):
    if len(data) < 32 or data[:8] != b'P5UIRES3':
        raise ValueError('Invalid UI bundle magic')
    count, size, checksum = struct.unpack_from('>III', data, 8)
    if not 0 < count <= 32 or size != len(data) or size > 1024 * 1024 or crc(data[32:]) != checksum:
        raise ValueError('Invalid UI bundle size/count/checksum')
    if 32 + count * 128 > size:
        raise ValueError('Truncated UI index')
    result = {}
    for i in range(count):
        record = data[32 + i * 128:160 + i * 128]
        volume, name = record[:16], record[16:64]
        if b'\0' not in volume or b'\0' not in name:
            raise ValueError('Unterminated UI resource name')
        volume, name = volume.split(b'\0')[0].decode('ascii'), name.split(b'\0')[0].decode('ascii')
        kind, offset, length, checksum, original_size, original_crc = struct.unpack_from('>4sIIIII', record, 64)
        if (volume not in ('menures', 'file_select', 'prosperowake') or kind not in (b'TIMG', b'SCRN') or
                offset % 32 or offset < 32 + count * 128 or length < 32 or offset + length > size):
            raise ValueError('Invalid UI resource range/type/scope')
        resource = data[offset:offset + length]
        if crc(resource) != checksum:
            raise ValueError('Invalid UI resource checksum')
        if kind == b'TIMG':
            w, h = struct.unpack_from('>HH', resource, 2)
            image = struct.unpack_from('>I', resource, 28)[0]
            if (resource[0] != 5 or resource[24] != 1 or not 0 < w <= 1024 or not 0 < h <= 1024 or
                    image < 32 or image % 32 or image + ((w + 3) // 4) * ((h + 3) // 4) * 32 > length):
                raise ValueError('Invalid UI texture payload')
        elif resource[:8] != b'P5UILAY1' or len(resource) != 32 + struct.unpack_from('>I', resource, 8)[0] * 64:
            raise ValueError('Invalid UI layout payload')
        key = volume, name
        if key in result:
            raise ValueError('Duplicate UI resource')
        result[key] = (resource, original_size, original_crc)
    return result


# GameCube A/B/X/Y/Z/L/R -> Cross/Square/Circle/Triangle/R1/L2/R2.
PROMPTS = {
    'ba_base_01.bti': 'Triangle', 'ba_base_02.bti': 'Square',
    'ba_base_03.bti': 'Cross', 'ba_base_04.bti': 'Cross',
    'ba_base_05.bti': 'R2', 'ba_base_z.bti': 'R1',
    'cross_key_00.bti': 'Dpad',
    'font_00.bti': 'Cross', 'font_01.bti': 'Square',
    'font_02.bti': 'Circle', 'font_03.bti': 'Triangle',
    'font_04.bti': 'L2', 'font_05.bti': 'R2', 'font_06.bti': 'R1',
    'font_07_01.bti': 'Left_Stick', 'font_07_02.bti': 'Left_Stick',
    'font_08.bti': 'Dpad', 'font_08_2.bti': 'Dpad_Left', 'font_08_up.bti': 'Dpad_Up',
    'font_09.bti': 'Right_Stick', 'font_09_02.bti': 'Right_Stick',
    'font_11.bti': 'Options', 'font_12.bti': 'Cross',
}
FOOTER = ('Cross', 'Square', 'L1', 'R1', 'Touch_Pad', 'Left_Stick_Click')


class RgbaImage:
    """The small RGBA canvases needed by controller artwork, not a general image API."""
    def __init__(self, width, height, pixels=None):
        self.size = (width, height)
        self.pixels = bytearray(width * height * 4) if pixels is None else bytearray(pixels)
        if len(self.pixels) != width * height * 4:
            raise ValueError('Incorrect RGBA image length')

    def getpixel(self, xy):
        x, y = xy
        at = (y * self.size[0] + x) * 4
        return self.pixels[at:at + 4]

    def paste(self, image, xy):
        x, y = xy
        w, h = image.size
        if x < 0 or y < 0 or x + w > self.size[0] or y + h > self.size[1]:
            raise ValueError('Image paste exceeds canvas')
        for row in range(h):
            at = ((y + row) * self.size[0] + x) * 4
            self.pixels[at:at + w * 4] = image.pixels[row * w * 4:(row + 1) * w * 4]

    def crop(self, box):
        left, top, right, bottom = box
        if not (0 <= left < right <= self.size[0] and 0 <= top < bottom <= self.size[1]):
            raise ValueError('Invalid image crop')
        return RgbaImage(right - left, bottom - top, b''.join(
            self.pixels[(y * self.size[0] + left) * 4:(y * self.size[0] + right) * 4]
            for y in range(top, bottom)))

    def resize(self, size):
        if size == self.size:
            return self
        width, height = size
        if width > self.size[0] or height > self.size[1]:
            raise ValueError('Controller artwork is only downsampled')
        out = RgbaImage(width, height)
        sx, sy = self.size[0] / width, self.size[1] / height
        # Area filtering with premultiplied alpha keeps small prompts smooth and
        # prevents invisible RGB values from creating fringes around their edges.
        for y in range(height):
            for x in range(width):
                left, top, right, bottom = x * sx, y * sy, (x + 1) * sx, (y + 1) * sy
                rgb, alpha = [0., 0., 0.], 0.
                for yy in range(math.floor(top), min(math.ceil(bottom), self.size[1])):
                    for xx in range(math.floor(left), min(math.ceil(right), self.size[0])):
                        weight = (min(right, xx + 1) - max(left, xx)) * (min(bottom, yy + 1) - max(top, yy))
                        r, g, b, a = self.getpixel((xx, yy))
                        alpha += a * weight
                        for i, c in enumerate((r, g, b)):
                            rgb[i] += c * a * weight
                rgba = [round(c / alpha) if alpha else 0 for c in rgb] + [round(alpha / (sx * sy))]
                at = (y * width + x) * 4
                out.pixels[at:at + 4] = bytes(max(0, min(255, c)) for c in rgba)
        return out


def read_png(path):
    """Read the bundled non-interlaced, 8-bit RGBA PNGs with zlib and PNG row filters."""
    data = Path(path).read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'Invalid PNG: {path}')
    at, compressed, dimensions = 8, bytearray(), None
    ended = False
    while at + 12 <= len(data):
        size, kind = struct.unpack_from('>I4s', data, at)
        end = at + 8 + size
        if end + 4 > len(data) or crc(data[at + 4:end]) != struct.unpack_from('>I', data, end)[0]:
            raise ValueError(f'Invalid PNG chunk: {path}')
        payload = data[at + 8:end]
        if kind == b'IHDR':
            if dimensions is not None or size != 13:
                raise ValueError('Invalid PNG header')
            w, h, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            if not (0 < w <= 1024 and 0 < h <= 1024) or (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError('Controller PNGs must be non-interlaced 8-bit RGBA')
            dimensions = (w, h)
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            ended = True
            break
        at = end + 4
    if dimensions is None or not ended:
        raise ValueError('Incomplete PNG')
    w, h = dimensions
    stride = w * 4
    raw = zlib.decompress(compressed)
    if len(raw) != (stride + 1) * h:
        raise ValueError('Incorrect PNG scanline length')
    out = bytearray()
    previous = bytearray(stride)
    for y in range(h):
        start = y * (stride + 1)
        method, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if method > 4:
            raise ValueError('Unknown PNG row filter')
        for x in range(stride):
            a, b, c = row[x - 4] if x >= 4 else 0, previous[x], previous[x - 4] if x >= 4 else 0
            if method == 1: predictor = a
            elif method == 2: predictor = b
            elif method == 3: predictor = (a + b) // 2
            elif method == 4:
                p = a + b - c
                distances = (abs(p - a), abs(p - b), abs(p - c))
                predictor = (a, b, c)[distances.index(min(distances))]
            else: predictor = 0
            row[x] = (row[x] + predictor) & 255
        out.extend(row)
        previous = row
    return RgbaImage(w, h, out)


@lru_cache(maxsize=None)
def prompt(name, size=96):
    # Keep the pack's shared 100x100 canvas for consistent button proportions.
    return read_png(ASSETS / 'xelu' / f'PS5_{name}.png').resize((size, size))


def bti(image):
    """GX RGB5A3, tiled 4x4. Transparent padding, one mip, linear filtering."""
    w, h = image.size
    out = bytearray(32 + ((w + 3) // 4) * ((h + 3) // 4) * 32)
    out[0:2] = bytes((5, 2))
    struct.pack_into('>HH', out, 2, w, h)
    out[20] = out[21] = out[24] = 1
    struct.pack_into('>I', out, 28, 32)
    for y in range(h):
        for x in range(w):
            r, g, b, a = image.getpixel((x, y))
            value = ((0x8000 | (r * 31 // 255) << 10 | (g * 31 // 255) << 5 | b * 31 // 255)
                     if a == 255 else ((a * 7 // 255) << 12 | (r // 17) << 8 | (g // 17) << 4 | b // 17))
            at = 32 + ((y // 4) * ((w + 3) // 4) + x // 4) * 32 + ((y % 4) * 4 + x % 4) * 2
            struct.pack_into('>H', out, at, value)
    return bytes(out)


def texture(name):
    image = prompt(PROMPTS[name])
    if name == 'cross_key_00.bti':
        # The minimap assembles four mirrored quadrants, not four whole D-pads.
        image = image.crop((0, 0, 48, 48))
    return bti(image)


def camera_prompt():
    # The eye is copied from the player's disc at runtime; only R3 is bundled.
    image = RgbaImage(128, 32)
    image.paste(prompt('Right_Stick_Click', 30), (25, 1))
    return bti(image)


def zoom_arrow():
    """Thin upward chevron with rounded ends and a soft charcoal outline."""
    image = RgbaImage(96, 96)
    points = ((24, 60), (48, 36), (72, 60))
    for y in range(96):
        for x in range(96):
            px, py = x + 0.5, y + 0.5
            distance = float('inf')
            for (ax, ay), (bx, by) in zip(points, points[1:]):
                dx, dy = bx - ax, by - ay
                t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
                distance = min(distance, math.hypot(px - ax - t * dx, py - ay - t * dy))
            alpha = max(0, min(1, (7 - distance) / 1.5))
            face = max(0, min(1, (4 - distance) / 1.25))
            grey = round(55 + 180 * face)
            at = (y * 96 + x) * 4
            image.pixels[at:at + 4] = bytes((grey, grey, grey, round(alpha * 255)))
    return bti(image)


def layout_recipe(file_select):
    rules = json.loads((ASSETS / 'layout.json').read_text())['file_select' if file_select else 'hud']
    textures = {'cent': 'ba_base_04.bti'} if file_select else {'b001': 'font_02.bti'}
    tags = sorted(set(rules) | set(textures))
    data = bytearray(32)
    data[:8] = b'P5UILAY1'
    struct.pack_into('>I', data, 8, len(tags))
    for tag in tags:
        if not 0 < len(tag.encode('ascii')) <= 4:
            raise ValueError(f'Invalid pane tag: {tag}')
        record = bytearray(64)
        record[:4] = tag.encode('ascii').rjust(4, b'\0')
        if tag in rules:
            struct.pack_into('>hhhh', record, 4, *rules[tag])
            record[12] |= 1
        if tag in textures:
            encoded = textures[tag].encode('ascii')
            record[16:16 + len(encoded)] = encoded
            record[12] |= 2
        data.extend(record)
    return bytes(data)


def shared_art(output):
    atlas = RgbaImage(100 * len(FOOTER), 100)
    for i, name in enumerate(FOOTER):
        atlas.paste(prompt(name, 100), (100 * i, 0))
    (output / 'menu-prompts.rgba').write_bytes(atlas.pixels)


def generate(output):
    output = Path(output).resolve()
    originals = json.loads((ASSETS / 'resources.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    for region in ('us', 'pal'):
        resources, descriptions = [], []
        for volume, key in (('menures', 'menures'), ('file_select', f'file_select_{region}')):
            for name, original in originals[key]['resources'].items():
                if name.endswith('.blo'):
                    replacement = layout_recipe(volume == 'file_select')
                else:
                    replacement = texture(name)
                resources.append((volume, name, original, replacement))
                item = {'volume': volume, 'name': name, 'size': len(replacement),
                        'sha256': sha(replacement), 'original_size': original['size'],
                        'original_sha256': original['sha256']}
                if name.endswith('.bti'):
                    item['prompt'] = f'xelu/PS5_{PROMPTS[name]}.png'
                    item['prompt_sha256'] = sha((ASSETS / item['prompt']).read_bytes())
                descriptions.append(item)
        camera = camera_prompt()
        resources.append(('prosperowake', 'camera_self.bti', {'size': 0, 'crc32': 0}, camera))
        descriptions.append({'volume': 'prosperowake', 'name': 'camera_self.bti',
                             'size': len(camera), 'sha256': sha(camera), 'original_size': 0,
                             'sources': ['xelu/PS5_Right_Stick_Click.png']})
        arrow = zoom_arrow()
        resources.append(('prosperowake', 'zoom_arrow.bti', {'size': 0, 'crc32': 0}, arrow))
        descriptions.append({'volume': 'prosperowake', 'name': 'zoom_arrow.bti',
                             'size': len(arrow), 'sha256': sha(arrow), 'original_size': 0,
                             'author': 'ProsperoWake', 'license': 'MIT', 'source': 'generate-controller-ui.py:zoom_arrow'})
        data = make_bundle(resources)
        decode_bundle(data)
        manifest = {'game_id': 'GZLP01' if region == 'pal' else 'GZLE01',
                    'format': 'P5UIRES3', 'source': SOURCE, 'author': 'Xelu', 'license': 'CC0-1.0',
                    'layout': 'Rules applied to layouts read from the player disc',
                    'resources': descriptions, 'bundle_sha256': sha(data)}
        folder = output / region
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'resources.bin').write_bytes(data)
        (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shared_art(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ASSETS)
    args = parser.parse_args()
    generate(args.output)


if __name__ == '__main__':
    main()
