#!/usr/bin/env python3
"""Build the HD pack of the public package from the pinned disc.

Nothing of the disc's pixels is in it: PlayStation button icons and a DualSense
diagram are drawn here, and the letters from the strokes in tools/font_strokes.py.
The disc gives the size of each glyph, and the fingerprints that find each texture
when the game loads it. Disc textures stay as they are.
"""
import argparse
import ctypes
import hashlib
import io
import json
import re
import shutil
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import font_hd
import font_strokes
import nfs_assets
from nfs_assets import find_chunks, jdlz_decode, texture_image, texture_payload

ROOT = Path(__file__).resolve().parent.parent
WHITE = '#f3f3f3'
if sys.platform == 'darwin' and Path('/opt/homebrew/lib/libcairo.2.dylib').exists():
    ctypes.CDLL('/opt/homebrew/lib/libcairo.2.dylib')
import cairosvg
font_hd.cairosvg = cairosvg

SECTOR = 0x800
DIAGRAM_BYTES = 65536
# Outer ends stay on the labels of the Xbox diagram; the inner ends follow
# ps5/src/pad_map.c.
ROUTES = (
    '1,5 48,5 62,46',                           # L2 / left trigger
    '1,40 30,40 88,122',                        # left stick
    '1,81 40,81 40,101 52,101',                 # d-pad
    '1,117 112,117 112,88 128,88',              # touch pad / Back
    '1,168 70,168 93,140',                      # L3
    '1,205 152,205 152,92 160,84',              # Options / Start
    '1,251 150,251 162,190 162,140',            # right stick
    '255,5 206,5 192,46',                       # R2 / right trigger
    '255,40 222,40 200,80',                     # Triangle / Y
    '255,81 230,81 212,101',                    # Circle / B
    '255,126 238,126 238,52 190,52 190,73',     # R1 / Black
    '255,168 246,168 246,215 18,215 18,73 65,73',  # L1 / White
    '255,210 200,210 193,122',                  # Cross / A
    '255,251 178,251 178,108',                  # Square / X
)


def svg(body, width=32, height=32):
    return f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{width}" height="{height}" viewBox="0 0 {width} {height}">{body}</svg>'


def icon(name):
    shape = ''
    stroke = f'fill="none" stroke="{WHITE}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"'
    if name in ('XBOX_A','XBOX_B','XBOX_X','XBOX_Y'):
        shape += f'<circle cx="16" cy="16" r="13" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.5"/>'
        geometry = {
            'XBOX_A':'<path d="M10 10L22 22M22 10L10 22"/>',
            'XBOX_B':'<circle cx="16" cy="16" r="8"/>',
            'XBOX_X':'<rect x="8.5" y="8.5" width="15" height="15"/>',
            'XBOX_Y':'<path d="M16 7L25 23H7Z"/>'}
        shape += f'<g {stroke}>{geometry[name]}</g>'
    elif name in ('XBOX_BLACK_BUTTON','XBOX_WHITE_BUTTON') or 'TRIGGER' in name:
        label = {'XBOX_BLACK_BUTTON':'R1','XBOX_WHITE_BUTTON':'L1'}.get(name)
        if label is None:
            label = 'L2' if 'LEFT_' in name else 'R2'
        shape += f'<rect x="1.5" y="6" width="29" height="20" rx="5" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.6"/>'
        shape += f'<text x="16" y="21" text-anchor="middle" fill="{WHITE}" font-size="14">{label}</text>'
    elif name == 'XBOX_START':
        shape += f'<circle cx="16" cy="16" r="13" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.5"/>'
        shape += f'<path d="M9 11H23M9 16H23M9 21H23" {stroke}/>'
    elif name == 'XBOX_BACK':
        shape += f'<rect x="2" y="7" width="28" height="18" rx="4" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.8"/>'
        for x in range(7,27,5):
            for y in (12,17,21):
                shape += f'<circle cx="{x}" cy="{y}" r="0.7" fill="{WHITE}"/>'
    elif 'ANALOG' in name:
        label = 'L' if 'LEFTANALOG' in name else 'R'
        shape += f'<circle cx="16" cy="16" r="12" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.7"/>'
        shape += f'<text x="16" y="22" text-anchor="middle" fill="{WHITE}" font-size="18">{label}</text>'
    elif 'DPAD' in name:
        shape += f'<path d="M11 3H21V11H29V21H21V29H11V21H3V11H11Z" fill="#111318" fill-opacity="0.85" stroke="{WHITE}" stroke-width="1.8"/>'
        directions = []
        if name != 'XBOX_DPAD':
            # The source uses identical two-way art for UP, DOWN and UP_DOWN,
            # and likewise for LEFT/RIGHT. Keep those aliases identical so
            # host texture fingerprints cannot choose conflicting artwork.
            directions = ['UP','DOWN'] if ('UP' in name or 'DOWN' in name) else ['LEFT','RIGHT']
        arrows = {'UP':'M16 4L12 9H20Z','DOWN':'M16 28L12 23H20Z','LEFT':'M4 16L9 12V20Z','RIGHT':'M28 16L23 12V20Z'}
        for direction in directions:
            shape += f'<path d="{arrows[direction]}" fill="{WHITE}"/>'
    else:
        raise ValueError('Unmapped icon '+name)
    return svg(shape)


def bhash(name):
    h = 0xFFFFFFFF
    for c in name.encode():
        h = (h*33 + c) & 0xFFFFFFFF
    return h


def disc_file(root, name):
    """ZDIR.BIN: records of hash, archive, sector, total sector, size, CRC."""
    data = (root/'ZDIR.BIN').read_bytes()
    wanted = bhash(name)
    for i in range(0, len(data), 24):
        record = struct.unpack_from('<6I', data, i)
        if record[0] == wanted:
            with (root/f'ZZDATA{record[1]}.BIN').open('rb') as f:
                f.seek(record[2]*SECTOR)
                return f.read(record[4])
    raise SystemExit('Not on the disc: '+name)


def textures(data):
    """Pixel data starts 0x78 bytes after the header in this game's TPKs."""
    for row in nfs_assets.textures(data):
        row['data_offset'] += 0x78
        yield row


def fnv(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte)*0x100000001b3) & 0xffffffffffffffff
    return value


def sampled(data):
    length = len(data)
    value = 2166136261 ^ length
    step = max(4, length//256) & ~3
    for i in range(0, length-3, step):
        value = ((value ^ struct.unpack_from('<I', data, i)[0])*16777619) & 0xffffffff
    return ((value ^ struct.unpack_from('<I', data, length-4)[0])*16777619) & 0xffffffff


def png(source, width, height):
    raw = cairosvg.svg2png(bytestring=source.encode(), output_width=width, output_height=height, unsafe=True)
    return Image.open(io.BytesIO(raw)).convert('RGBA')


def diagram():
    with tempfile.TemporaryDirectory() as work:
        picture = Path(work)/'dualsense-front.png'
        shutil.copyfile(ROOT/'ps5/assets/dualsense-front.png', picture)
        body = f'<image x="25" y="60" width="206" height="138" xlink:href="file://{picture}"/>'
        for points in ROUTES:
            body += f'<polyline points="{points}" fill="none" stroke="#e6e8e8" stroke-width="1.3"/>'
        for label, x in (('L2', 62), ('R2', 192)):
            body += f'<text x="{x}" y="57" text-anchor="middle" fill="white" font-size="10">{label}</text>'
        return png(svg(body, 256, 256), 1024, 1024)


def font_atlas(data, row, name):
    alpha = texture_image(data, row).getchannel('A')
    cells = {g['ch']: g for g in font_hd.font_tables(data)[name.lower()]}
    scale = 8
    hd = np.zeros((alpha.height*scale, alpha.width*scale))
    table = {**font_strokes.LATIN, **font_strokes.SIGNS}
    font_strokes.redraw(hd, name, cells, np.asarray(alpha, float)/255, table, scale)
    image = Image.new('RGBA', (alpha.width*scale, alpha.height*scale), (255, 255, 255, 255))
    image.putalpha(Image.fromarray((hd.clip(0, 1)*255).round().astype('uint8')))
    return image


class Pack:
    def __init__(self, out):
        self.out = out
        self.records = {}
        self.names = []

    def add(self, name, image, color, width, height, payload):
        content = image.tobytes('raw', 'BGRA')
        filename = hashlib.sha256(content).hexdigest()[:32]+'.rgba'
        (self.out/filename).write_bytes(struct.pack('<4sIII', b'NHD1', image.width, image.height, len(content))+content)
        sample, strong = sampled(payload), fnv(payload)
        key = (color, width, height, len(payload), sample, strong)
        if self.records.setdefault(key, filename) != filename:
            raise SystemExit('Conflicting replacement fingerprints: '+name)
        self.names.append(name)

    def write(self):
        rows = sorted(self.records.items(), key=lambda item: item[0][4])
        body = b''.join(struct.pack('<9I48s', *key[:5], *self.size(name), key[5] & 0xffffffff, key[5] >> 32, name.encode())
                        for key, name in rows)
        (self.out/'manifest.bin').write_bytes(struct.pack('<4sIII', b'NHDX', 1, len(rows), 84)+body)
        active = set(self.records.values())
        for stale in self.out.glob('*.rgba'):
            if stale.name not in active:
                stale.unlink()
        return len(rows)

    def size(self, filename):
        with (self.out/filename).open('rb') as f:
            return struct.unpack('<4sIII', f.read(16))[1:3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=ROOT/'source/Need for Speed Underground 2')
    parser.add_argument('--out', type=Path, default=ROOT/'build/hd')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    root = args.game/'NFSUNDER'
    pack = Pack(args.out)

    data = disc_file(root, 'GLOBAL\\GLOBALB.BUN')
    for row in textures(data):
        if row['name'].startswith('XBOX_'):
            size = row['width']*row['height']*(8 if row['format'] == 0xC else 16)//16
            pack.add(row['name'], png(icon(row['name']), 256, 256), row['format'], row['width'], row['height'],
                     texture_payload(data, row)[:size])

    data = disc_file(root, 'GLOBAL\\GLOBALA.BUN')
    name = 'CONDUITMDITC_TT21I'
    row = next(r for r in textures(data) if r['name'] == name)
    pack.add(name, font_atlas(data, row, name), row['format'], row['width'], row['height'],
             texture_payload(data, row)[:row['width']*row['height']])

    # Each entry {hash, offset, compressed, size, 256, 0} of DYNTEX.BIN names a JDLZ
    # stream: a 256x256 DXT3 surface, then its texture record.
    data = disc_file(root, 'GLOBAL\\DYNTEX.BIN')
    parts = {ident: (pos+8, stop) for ident, pos, stop in find_chunks(data)}
    first, last = parts[0x33310003]
    picture = diagram()
    for pos in range(first, last, 24):
        entry = struct.unpack_from('<6I', data, pos)
        surface = jdlz_decode(data[entry[1]:entry[1]+entry[2]])
        name = surface[DIAGRAM_BYTES+12:DIAGRAM_BYTES+36].split(b'\0')[0]
        if name.startswith(b'LOADING_CONTROLLER_XBOX'):
            pack.add(name.decode(), picture, 0xE, 256, 256, surface[:DIAGRAM_BYTES])
    print(f'HD pack: {len(pack.names)} textures, {pack.write()} fingerprints')


if __name__ == '__main__':
    main()
