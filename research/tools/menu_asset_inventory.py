#!/usr/bin/env python3
"""Inspect menu/preview assets read-only, validating PNG framing independently.

Pillow independently validates decoded images. When --decoded is given, compare
RGBA bytes emitted by the existing C++ assets_probe diagnostic per archive stem.
Generated files remain under ignored research evidence; no game writes occur.
"""
import argparse
import io
import json
from pathlib import Path
import struct
import zlib
from cpr_layout import inspect


def png_extent(data, offset):
    if data[offset:offset + 8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Missing PNG signature')
    cursor = offset + 8
    while cursor + 12 <= len(data):
        size = struct.unpack_from('>I', data, cursor)[0]
        end = cursor + 12 + size
        if end > len(data):
            raise ValueError('PNG chunk exceeds archive')
        kind = data[cursor + 4:cursor + 8]
        body = data[cursor + 4:cursor + 8 + size]
        if zlib.crc32(body) != struct.unpack_from('>I', data, cursor + 8 + size)[0]:
            raise ValueError('PNG chunk CRC mismatch')
        cursor = end
        if kind == b'IEND':
            if size:
                raise ValueError('Nonempty IEND')
            return cursor - offset
    raise ValueError('Missing IEND')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--decoded', type=Path)
    args = parser.parse_args()
    game, output = args.game.resolve(), args.output.resolve()
    if output == game or game in output.parents:
        parser.error('Evidence must remain outside the game installation')
    from PIL import Image
    groups = ['titlemenu/titlemenu', 'font/font', 'stageselect/stageselect',
              'option/option', 'option/optiontit', 'custom/custom']
    paths = [game / f'data/data/stable/sprite/{group}.cpr' for group in groups]
    paths += [game / f'data/concept/PW{i}/extend/cosspr.cpr' for i in range(1, 6)]
    records = []
    for path in paths:
        data = path.read_bytes()
        metadata = inspect(path)
        for record in metadata['records']:
            if record['type'] != 'DDS ':
                continue
            offset = record['payload_offset']
            extent = png_extent(data, offset)
            pixels = Image.open(io.BytesIO(data[offset:offset + extent])).convert('RGBA')
            entry = {'archive': path.relative_to(game).as_posix(), 'name': record['name'],
                     'original_size': record['payload_bytes'], 'png_size': extent,
                     'dimensions': list(pixels.size), 'crc_valid': True}
            if args.decoded:
                folder = path.parts[-3] if path.stem == 'cosspr' else 'stable'
                decoded = args.decoded / folder / f'{path.stem}-{record["index"]}.rgba'
                if decoded.read_bytes() != pixels.tobytes():
                    raise ValueError(f'RGBA mismatch: {path}/{record["name"]}')
                entry['cpp_rgba_equal'] = True
            records.append(entry)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'images': records,
        'inline_size_corrections': sum(r['original_size'] != r['png_size'] for r in records)}, indent=2) + '\n')
    print(f'Validated {len(records)} PNG images')


if __name__ == '__main__':
    main()
