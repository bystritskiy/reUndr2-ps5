"""Small, bounded readers for this revision's language and Xbox texture packs.

References: xan1242/xnfstpktool (TPK layout), MWisBest/OpenNFSTools (JDLZ).
No executable or proprietary decompressor is used.
"""
import struct
import io
from collections import defaultdict, deque
from pathlib import Path
from PIL import Image


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def chunks(data, start=0, end=None):
    end = len(data) if end is None else end
    pos = start
    while pos < end:
        if pos + 8 > end:
            raise ValueError('Truncated chunk header')
        ident, size = struct.unpack_from('<II', data, pos)
        stop = pos + 8 + size
        if stop > end:
            raise ValueError('Chunk extends beyond parent')
        yield ident, pos, stop
        pos = stop


def find_chunks(data, start=0, end=None):
    for ident, pos, stop in chunks(data, start, end):
        yield ident, pos, stop
        if ident & 0x80000000:
            yield from find_chunks(data, pos + 8, stop)


def language(data):
    if u32(data, 0) != 0x39000 or u32(data, 4) + 8 != len(data):
        raise ValueError('Unexpected language chunk')
    count = u32(data, 8)
    base = 12 + count * 8
    rows = []
    for i in range(count):
        key, offset = struct.unpack_from('<II', data, 12 + i * 8)
        pos = base + offset
        stop = pos
        while stop + 2 <= len(data) and data[stop:stop+2] != b'\0\0':
            stop += 2
        if stop + 2 > len(data):
            raise ValueError('Unterminated language string')
        rows.append((key, data[pos:stop].decode('utf-16le')))
    return rows


def write_language(rows):
    table, strings = bytearray(), bytearray()
    for key, text in rows:
        table += struct.pack('<II', key, len(strings))
        strings += text.encode('utf-16le') + b'\0\0'
    body = struct.pack('<I', len(rows)) + table + strings
    body += b'\0' * (-len(body) % 4)
    return struct.pack('<II', 0x39000, len(body)) + body


def jdlz_decode(data):
    if data[:8] != b'JDLZ\x02\x10\0\0' or u32(data, 12) != len(data):
        raise ValueError('Invalid JDLZ header')
    expected = u32(data, 8)
    if expected > 64 * 1024 * 1024:
        raise ValueError('JDLZ output too large')
    out, pos, flags, matches = bytearray(), 16, 1, 1
    while len(out) < expected:
        if flags == 1:
            flags = data[pos] | 256
            pos += 1
        if matches == 1:
            matches = data[pos] | 256
            pos += 1
        if flags & 1:
            a, b = data[pos:pos+2]
            pos += 2
            if matches & 1:
                length, distance = (b | ((a & 240) << 4)) + 3, (a & 15) + 1
            else:
                length, distance = (a & 31) + 3, (b | ((a & 224) << 3)) + 17
            if distance > len(out) or len(out) + length > expected:
                raise ValueError('Invalid JDLZ back-reference')
            for _ in range(length):
                out.append(out[-distance])
            matches >>= 1
        else:
            out.append(data[pos])
            pos += 1
        flags >>= 1
    return bytes(out)


def jdlz_literal(data):
    # Literal-only stream: deterministic, modest size increase for UI assets.
    body = bytearray()
    for pos in range(0, len(data), 8):
        body.append(0)
        if pos == 0:
            body.append(0)
        body += data[pos:pos+8]
    if not data:
        body += b'\0\0'
    elif len(data) % 8 == 0:
        body.append(0)  # Xbox eagerly reloads the next flag after the final token.
    result = b'JDLZ\x02\x10\0\0' + struct.pack('<II', len(data), 16+len(body)) + body
    if jdlz_decode(result) != data:
        raise ValueError('JDLZ roundtrip failed')
    return result


def jdlz_encode(data):
    """Greedy JDLZ with both distance classes and Xbox terminal flag reloads."""
    body = bytearray()
    history = defaultdict(lambda:deque(maxlen=32))
    pos, mask1, mask2, flag1, flag2 = 0, 0, 0, 0, 0
    while pos < len(data):
        if mask1 == 0:
            flag1 = len(body)
            body.append(0)
            mask1 = 1
        if mask2 == 0:
            flag2 = len(body)
            body.append(0)
            mask2 = 1
        length, distance = 0, 0
        if pos+3 <= len(data):
            candidates = history.get(data[pos:pos+3],())
            for previous in reversed(candidates):
                back = pos-previous
                if back > 2064:
                    break
                limit = min(4098 if back <= 16 else 34,len(data)-pos)
                if limit <= length:
                    continue
                match = 3
                while match < limit and data[pos+match] == data[pos+match-back]:
                    match += 1
                if match > length:
                    length, distance = match, back
                    if match == min(4098,len(data)-pos):
                        break
        consumed = length if length >= 3 else 1
        if length >= 3:
            body[flag1] |= mask1
            if distance <= 16:
                body[flag2] |= mask2
                body.extend(((distance-1) | (((length-3)>>8)<<4),(length-3)&255))
            else:
                back = distance-17
                body.extend(((length-3) | ((back>>8)<<5),back&255))
            mask2 = (mask2 << 1) & 255
        else:
            body.append(data[pos])
        for i in range(pos,min(pos+consumed,len(data)-2)):
            history[data[i:i+3]].append(i)
        pos += consumed
        mask1 = (mask1 << 1) & 255
    # The Xbox decoder (guest sub_00147A80) reloads depleted flag groups
    # BEFORE checking the remaining compressed byte count, even after the last
    # output byte. Omitting these bytes causes counter underflow and overrun.
    if not data:
        body += b'\0\0'
    else:
        if mask1 == 0:
            body.append(0)
        if mask2 == 0:
            body.append(0)
    result = b'JDLZ\x02\x10\0\0'+struct.pack('<II',len(data),16+len(body))+body
    if jdlz_decode(result) != data:
        raise ValueError('JDLZ compressed roundtrip failed')
    if jdlz_decode_xbox(result) != data:
        raise ValueError('Xbox JDLZ compressed roundtrip failed')
    return result


def jdlz_decode_xbox(data):
    """Independent check of the compressed-byte termination used by this XBE."""
    if data[:8] != b'JDLZ\x02\x10\0\0' or len(data) < 18 or u32(data,12) != len(data):
        raise ValueError('Invalid Xbox JDLZ header')
    expected = u32(data,8)
    pos, flags, matches = 18, data[16] | 256, data[17] | 256
    remaining = len(data)-18
    out = bytearray()
    while remaining:
        if remaining < 0:
            raise ValueError('Xbox JDLZ compressed counter underflow')
        if flags & 1:
            a,b = data[pos:pos+2]
            pos += 2
            remaining -= 2
            if matches & 1:
                length,distance = (b | ((a&240)<<4))+3,(a&15)+1
            else:
                length,distance = (a&31)+3,(b | ((a&224)<<3))+17
            if distance > len(out) or len(out)+length > expected:
                raise ValueError('Xbox JDLZ output overrun')
            for _ in range(length):
                out.append(out[-distance])
            matches >>= 1
        else:
            if len(out) < expected:
                out.append(data[pos])
                pos += 1
            remaining -= 1
        flags >>= 1
        if flags == 1:
            if pos >= len(data):
                raise ValueError('Missing Xbox terminal literal flag')
            flags = data[pos] | 256
            pos += 1
            remaining -= 1
        if matches == 1:
            if pos >= len(data):
                raise ValueError('Missing Xbox terminal match flag')
            matches = data[pos] | 256
            pos += 1
            remaining -= 1
    if len(out) != expected:
        raise ValueError('Xbox JDLZ output length mismatch')
    return bytes(out)


def textures(data):
    for ident, start, stop in chunks(data):
        if ident != 0xB3300000:
            continue
        parts = {key: (pos+8, end) for key, pos, end in find_chunks(data, start+8, stop)}
        first, last = parts[0x33310004]
        count = (last-first)//124
        if first + count*124 != last:
            raise ValueError('Unexpected texture record size')
        pixels = parts[0x33310005][0]
        compressed = parts.get(0x33310003)
        blob = parts[0x33320001][0] + 0x78
        for i in range(count):
            pos = first+i*124
            name = data[pos+12:pos+36].split(b'\0')[0].decode('ascii')
            fmt = u32(data, pixels+i*60+12)
            offset, palette, size, palette_size, base = struct.unpack_from('<5I',data,pos+48)
            width, height = struct.unpack_from('<HH',data,pos+68)
            row = dict(name=name, hash=u32(data,pos+36), record=pos, format=fmt,
                       width=width, height=height, size=size, mipmaps=data[pos+78],
                       data_offset=blob+offset, pack=start)
            if compressed:
                entry = compressed[0]+i*20
                key, absolute, length, _, _ = struct.unpack_from('<5I',data,entry)
                if key != row['hash']:
                    raise ValueError('Compressed texture hash mismatch')
                row.update(compressed_entry=entry, data_offset=absolute, compressed_size=length)
            yield row


def texture_payload(data, row):
    pos = row['data_offset']
    if 'compressed_entry' in row:
        return jdlz_decode(data[pos:pos+row['compressed_size']])
    return data[pos:pos+row['size']]


def texture_image(data, row):
    payload = texture_payload(data, row)
    width, height = row['width'], row['height']
    if row['format'] == 0xE:  # DXT3, already block-linear on Xbox.
        return Image.frombytes('RGBA', (width,height), payload[:width*height], 'bcn', (2, 'DXT3'))
    if row['format'] == 0xC:
        return Image.frombytes('RGBA', (width,height), payload[:width*height//2], 'bcn', (1, 'DXT1'))
    raise ValueError(f"Unsupported texture format {row['format']:x}")


def encode_texture(image, row, original):
    if row['format'] not in (0xC, 0xE):
        raise ValueError('Replacement must use the original DXT format')
    output = bytearray()
    for level in range(row['mipmaps']):
        size = (max(1,row['width'] >> level), max(1,row['height'] >> level))
        mip = image.convert('RGBA').resize(size, Image.Resampling.LANCZOS)
        stream = io.BytesIO()
        mip.save(stream, format='DDS', pixel_format='DXT3' if row['format'] == 0xE else 'DXT1')
        output += stream.getvalue()[128:]
    if len(output) != row['size']:
        raise ValueError('Replacement mip chain differs in size')
    return bytes(output) + original[row['size']:]


def replace_textures(data, replacements):
    """Replace by texture hash, retaining names, formats and all mip levels."""
    out = bytearray(data)
    streams = []
    found = set()
    all_rows = list(textures(data))
    for row in all_rows:
        key = row['hash']
        if key not in replacements:
            continue
        found.add(key)
        payload = texture_payload(data,row)
        updated = encode_texture(replacements[key],row,payload)
        if 'compressed_entry' in row:
            streams.append((row, jdlz_encode(updated)))
        else:
            pos = row['data_offset']
            out[pos:pos+row['size']] = updated
    if found != replacements.keys():
        raise ValueError('Replacement texture was not found')
    # This revision's compressed DYNTEX has one TPK and one stream per texture.
    # Rebuild the streams, then fix every absolute offset and containing size.
    if streams:
        if len({r['pack'] for r in all_rows}) != 1:
            raise ValueError('Compressed replacement requires a single TPK')
        by_offset = sorted((r for r in all_rows if 'compressed_entry' in r),key=lambda r:r['data_offset'])
        changed = {r['hash']: payload for r,payload in streams}
        cursor = 0
        rebuilt = bytearray()
        offsets = {}
        for row in by_offset:
            pos, size = row['data_offset'], row['compressed_size']
            if pos < cursor:
                raise ValueError('Overlapping compressed streams')
            rebuilt += out[cursor:pos]
            payload = changed.get(row['hash'],data[pos:pos+size])
            offsets[row['hash']] = (len(rebuilt),len(payload))
            rebuilt += payload
            cursor = pos+size
        rebuilt += out[cursor:]
        delta = len(rebuilt)-len(data)
        for row in by_offset:
            struct.pack_into('<II',rebuilt,row['compressed_entry']+4,*offsets[row['hash']])
        first_stream, last_stream = by_offset[0]['data_offset'], cursor
        for ident,pos,stop in find_chunks(data):
            if pos < first_stream and stop >= last_stream:
                struct.pack_into('<I',rebuilt,pos+4,stop-pos-8+delta)
        out = rebuilt
    # Decode all replacement images and ensure unrelated compressed data survived.
    new_rows = {r['hash']:r for r in textures(out)}
    for row in all_rows:
        if 'compressed_entry' in row and row['hash'] not in replacements:
            if texture_payload(data,row) != texture_payload(out,new_rows[row['hash']]):
                raise ValueError('Unrelated compressed texture changed')
    for key in replacements:
        texture_image(out,new_rows[key]).load()
    return bytes(out)


def locale(data):
    if data[:4] != b'LOCH' or data[20:24] != b'LOCI' or data[120:124] != b'LOCL':
        raise ValueError('Unexpected locale format')
    count = u32(data,132)
    offsets = [u32(data,136+i*4) for i in range(count+1)]
    rows = [data[120+a:120+b].decode('utf-16le').rstrip('\0') for a,b in zip(offsets,offsets[1:])]
    return rows


def write_locale(original, rows):
    if len(rows) != len(locale(original)):
        raise ValueError('Locale entry count changed')
    header = bytearray(original[:120])
    strings, offsets = bytearray(), []
    first = 16+4*(len(rows)+1)
    for text in rows:
        offsets.append(first+len(strings))
        strings += text.encode('utf-16le')+b'\0\0\0\0'
    strings += b'\0' * (-(first+len(strings)) % 4)
    offsets.append(first+len(strings))
    body = b'LOCL'+struct.pack('<III',first+len(strings),0,len(rows))+struct.pack('<'+'I'*len(offsets),*offsets)+strings
    result = bytes(header)+body
    if locale(result) != rows:
        raise ValueError('Locale roundtrip failed')
    return result
