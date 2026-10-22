#!/usr/bin/env python3
"""Read-only CPR layout inspection for the Steam build of Bullet Witch.

Field interpretation reference: Luigi Auriemma's bullet_witch_cpr.bms 0.1.1,
https://mirror.aluigi.org/bms/bullet_witch_cpr.bms . No game data is modified.
Unknown fields retain their offsets rather than being assigned speculative names.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if len(data) < 0x60 or data[:4] != b'CPR1':
        raise ValueError('Not a CPR1 container')
    if data[8:12] != bytes.fromhex('44332211'):
        raise ValueError('Unsupported byte order')
    h = struct.unpack_from('<15I', data, 4)
    table_size, auxiliary_size, names_size, gap_size, first_size = h[3:8]
    if table_size % 0x30:
        raise ValueError('Record table size is not a multiple of 48')
    names_start = 0x60 + table_size + auxiliary_size
    names_end = names_start + names_size
    first_base = names_end + gap_size
    second_base = first_base + first_size
    if names_end > len(data):
        raise ValueError('Name table exceeds file')
    records = []
    for index, pos in enumerate(range(0x60, 0x60 + table_size, 0x30)):
        kind = data[pos:pos+4].decode('ascii', errors='replace')
        fields = struct.unpack_from('<11I', data, pos+4)
        name_pos = names_start + fields[0]
        if not names_start <= name_pos < names_end:
            raise ValueError(f'Record {index}: invalid name offset')
        end = data.find(b'\0', name_pos, names_end)
        if end < 0:
            raise ValueError(f'Record {index}: unterminated name')
        name = data[name_pos:end].decode('utf-8', errors='replace')
        offset = None
        size = 0
        if fields[8]:
            offset, size = first_base + fields[7], fields[8]
        elif fields[10]:
            offset, size = second_base + fields[9], fields[10]
        if fields[2] != 0xffffffff:
            offset = 0x28 + fields[2]
        if offset is not None and offset + size > len(data):
            raise ValueError(f'Record {index}: payload exceeds file')
        payload = data[offset:offset+size] if offset is not None else b''
        record = {'index':index, 'table_offset':pos, 'type':kind, 'name':name,
                  'fields_after_type':list(fields), 'payload_offset':offset,
                  'payload_bytes':size, 'payload_prefix_hex':payload[:32].hex()}
        if payload.startswith(b'\x89PNG\r\n\x1a\n') and len(payload)>=24:
            record['png_dimensions'] = list(struct.unpack_from('>II', payload, 16))
        records.append(record)
    return {'bytes':len(data), 'sha256':hashlib.sha256(data).hexdigest(),
            'header_u32_from_0x04':list(h), 'names_offset':names_start,
            'names_bytes':names_size, 'first_payload_base':first_base,
            'second_payload_base':second_base, 'records':records}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game',type=Path)
    ap.add_argument('output',type=Path)
    args=ap.parse_args()
    game=args.game.resolve(); out=args.output.resolve()
    if out == game or game in out.parents:
        ap.error('Output must be outside the downloaded game directory')
    out.mkdir(parents=True,exist_ok=True)
    types=Counter(); signatures=Counter(); failures=[]; rows=[]; samples={}
    selected={
      'data/shader/shaders.cpr',
      'data/data/stable/graphics/chara/PW0/texture/texture.cpr',
      'data/data/stable/graphics/chara/PW0/model/mesh.cpr',
      'data/data/stable/graphics/chara/PW0/motion/compress/bundler.cpr',
      'data/data/stable/graphics/chara/PW0/hit/cpe_hit.cpr',
      'data/data/stable/script/root.cpr',
      'data/data/stable/script/st01_00.cpr',
      'data/data/stable/ai/pack.cpr',
      'data/data/stable/graphics/bg/01/areadata/bundler.cpr',
      'data/data/stable/sprite/gameover/gameover.cpr',
    }
    for path in sorted(game.rglob('*.cpr')):
        rel=path.relative_to(game).as_posix()
        try:
            result=inspect(path)
            kinds=Counter(r['type'] for r in result['records'])
            types.update(kinds)
            for r in result['records']:
                if r['payload_bytes']: signatures.update([r['payload_prefix_hex'][:16]])
            rows.append({'path':rel,'bytes':result['bytes'],'sha256':result['sha256'],
                         'records':len(result['records']),'types':dict(kinds)})
            if rel in selected:samples[rel]=result
        except (ValueError,struct.error) as e:
            failures.append({'path':rel,'error':str(e)})
    summary={'archives_parsed':len(rows),'errors':failures,'record_types':dict(types),
             'nonempty_payload_prefixes':dict(signatures.most_common(40)),
             'selected_paths_not_found':sorted(selected-set(samples))}
    (out/'cpr-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'cpr-inventory.json').write_text(json.dumps(rows,indent=2)+'\n')
    (out/'cpr-samples.json').write_text(json.dumps(samples,indent=2)+'\n')
    print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
