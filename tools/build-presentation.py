#!/usr/bin/env python3
"""Export the reUndr 2 icon and generated PS5 backgrounds; validate BC7 by decoding."""
from pathlib import Path
import hashlib
import json
import struct

import etcpak
import texture2ddecoder
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT/'ps5/assets/icon.webp'
BACKGROUND = ROOT/'ps5/assets/background-source-v7-outpaint.png'
OUT = ROOT/'ps5/sce_sys'
W, H = 3840, 2160


def dds_header():
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    pixel_format = struct.pack('<2I4s5I', 32, 4, b'DX10', 0, 0, 0, 0, 0)
    header = struct.pack('<4s7I44x', b'DDS ', 124, flags, H, W, W * H, 1, 1)
    header += pixel_format + struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    return header + struct.pack('<5I', 98, 3, 0, 1, 1)


def main():
    OUT.mkdir(exist_ok=True)
    with Image.open(ART) as art:
        art.convert('RGB').resize((512, 512), Image.Resampling.LANCZOS).save(OUT/'icon0.png')
    with Image.open(BACKGROUND) as source:
        if abs(source.width / source.height - W / H) > 0.02:
            raise SystemExit('Background must have a 16:9 composition')
        source_size = source.size
        # The user chose canvas extension specifically to retain this original.
        original_preserved = None
        original_path = ROOT/'ps5/assets/background-source-v7.png'
        if BACKGROUND.name == 'background-source-v7-outpaint.png' and original_path.exists():
            with Image.open(original_path) as original:
                if source.size != (2048, 1152) or source.crop((376, 211, 2048, 1152)).convert('RGB').tobytes() != original.convert('RGB').tobytes():
                    raise SystemExit('Outpainting changed pixels of the selected original')
            original_preserved = {'source': str(original_path.relative_to(ROOT)),
                'rect': [376, 211, 1672, 941], 'rgb_pixels_unchanged': True}
        city = source.convert('RGBA').resize((W, H), Image.Resampling.LANCZOS)
    city.putalpha(255)
    params = etcpak.BC7CompressBlockParams()
    params.m_mode_mask = 1 << 1  # Implicit opaque alpha; mode 6 can round it to 254.
    params.m_uber_level = 2
    blocks = etcpak.compress_bc7(city.tobytes(), W, H, params)
    if len(blocks) != W * H:
        raise SystemExit('Incorrect BC7 surface length')
    decoded = Image.frombytes('RGBA', (W, H),
        texture2ddecoder.decode_bc7(blocks, W, H), 'raw', 'BGRA')
    if decoded.getchannel('A').getextrema() != (255, 255):
        raise SystemExit('Unexpected background transparency')
    error = sum(ImageStat.Stat(ImageChops.difference(
        decoded.convert('RGB'), city.convert('RGB'))).mean) / 3
    if error > 5:
        raise SystemExit('BC7 decode differs excessively from source')
    payload = dds_header() + blocks
    for name in ('pic0.dds', 'pic1.dds'):
        target = OUT/name
        target.write_bytes(payload)
        if target.read_bytes() != payload:
            raise SystemExit('DDS write verification failed: '+name)
    city.convert('RGB').resize((1920, 1080), Image.Resampling.LANCZOS).save(OUT/'pic1.png')
    preview = ROOT/'build/presentation'
    preview.mkdir(parents=True, exist_ok=True)
    decoded.convert('RGB').resize((1920, 1080), Image.Resampling.LANCZOS).save(preview/'background-dds-preview.png')
    report = {
        'date': '2026-10-31', 'title_id': 'PPSA99027',
        'generator': 'built-in image_gen',
        'source': str(BACKGROUND.relative_to(ROOT)),
        'source_sha256': hashlib.sha256(BACKGROUND.read_bytes()).hexdigest(),
        'source_size': list(source_size), 'output_size': [W, H],
        'format': 'DX10 DDS BC7_UNORM, mode 1, opaque, one mip',
        'independent_decoder': 'texture2ddecoder',
        'mean_absolute_rgb_error_255': round(error, 4),
        'console_visual_verified': False,
        'limitations': ['4K export resized from the generated master'],
        'outputs': {n: {'size': (OUT/n).stat().st_size,
            'sha256': hashlib.sha256((OUT/n).read_bytes()).hexdigest()}
            for n in ('icon0.png', 'pic0.dds', 'pic1.dds', 'pic1.png')},
    }
    if original_preserved:
        report['original_preservation'] = original_preserved
    (ROOT/'docs/presentation-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
