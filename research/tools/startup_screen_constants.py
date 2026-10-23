#!/usr/bin/env python3
"""Reproduce reviewed startup screen constants from the local unpacked image.

Uses the existing hash-bound engine_image loader. Generated records belong under
ignored research evidence, never in the game installation. This reads static
bytes, not runtime timing or visual equivalence.
"""
import json
from pathlib import Path
import struct

import engine_image as image

OUTPUT = Path(__file__).resolve().parents[1] / 'evidence/reconstruction/startup-title'
CONSTANTS = [0x6c13a4, 0x6c139c, 0x6c13a0, 0x6c1398, 0x6c1d98,
             0x6c1d94, 0x6c1da4, 0x95a6a4, 0x6c1ff8, 0x82cfa0, 0x6c1d88]
result = {
    'image_sha256': image.META['sha256'],
    'constants': {hex(address): {
        'hex': image.raw(address, 4).hex(),
        'float': struct.unpack('<f', image.raw(address, 4))[0]
    } for address in CONSTANTS},
    'sprites': {},
    'loading_groups': [image.string(image.u32(0x9595e0 + index * 12))
                       for index in range(10)],
    'overlay_task': {
        'name': image.string(0x831eb8),
        'on_create': '0x4ac950',
        'priority_instruction': image.raw(0x4ac965, 7).hex(),
        'priority': image.u32(0x4ac968),
        'vtable': '0x82bd50',
        'draw_slot': hex(image.u32(0x82bd6c)),
    },
    'blend_table': list(struct.unpack('<16I', image.raw(0x841050, 64))),
}
for address in [0x95a6a8, 0x95a6d8, 0x95a708]:
    words = struct.unpack('<12I', image.raw(address, 48))
    floats = struct.unpack('<12f', image.raw(address, 48))
    result['sprites'][hex(address)] = {
        'sprite_id': words[0], 'position': floats[1:3],
        'crop': floats[3:7], 'scale': floats[7:9],
        'angle': floats[9], 'color': hex(words[10]), 'sort_offset': words[11],
    }
OUTPUT.mkdir(parents=True, exist_ok=True)
(OUTPUT / 'screen-constants.json').write_text(json.dumps(result, indent=2) + '\n')
source = image.ROOT / 'evidence/native-startup/text-disassembly.txt'
lines = []
for line in source.read_text().splitlines():
    try:
        address = int(line.split(':')[0].strip(), 16)
    except ValueError:
        continue
    if 0x4ac950 <= address < 0x4ac96d:
        lines.append(line)
(OUTPUT / 'overlay-priority.txt').write_text('\n'.join(lines) + '\n')
print('Wrote screen constants and overlay-priority witness under research evidence')
