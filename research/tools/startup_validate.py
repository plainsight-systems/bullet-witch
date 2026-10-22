#!/usr/bin/env python3
"""Check the scoped Lua reader against all stable/script .lub members.

Checks format, byte consumption, opcode names, constant/prototype indexes and
jump bounds. This is structural validation, not execution or VM verification.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
from local_inputs import game_directory
from cpr_layout import inspect
from layout_probe import dpk
from lua50_read import parse

research = Path(__file__).resolve().parents[1]
game = game_directory()
results = []
opcodes = Counter()

def walk(f):
    yield f
    for child in f['children']:
        yield from walk(child)

for path in sorted((game/'data/data/stable/script').glob('*.cpr')):
    blob = path.read_bytes()
    for record in inspect(path)['records']:
        if record['payload_offset'] is None:
            continue
        off = record['payload_offset']
        data = blob[off:off+record['payload_bytes']]
        if not data.startswith(b'dpk\0'):
            continue
        for e in dpk(data)['entries']:
            if not e['name'].endswith('.lub'):
                continue
            chunk = data[e['offset']:e['offset']+e['stored_bytes']]
            row = dict(archive=str(path.relative_to(game)), member=e['name'], bytes=len(chunk), sha256=hashlib.sha256(chunk).hexdigest())
            try:
                assert e['stored_bytes'] == e['declared_bytes']
                decoded = parse(chunk)
                funcs = list(walk(decoded['root']))
                for f in funcs:
                    opcodes.update(i['op'] for i in f['code'])
                row.update(status='pass', consumed_bytes=decoded['bytes_consumed'], prototypes=len(funcs), instructions=sum(len(f['code']) for f in funcs))
            except (ValueError, AssertionError) as error:
                row.update(status='fail', error=str(error))
            results.append(row)
summary = dict(members=len(results), passed=sum(r['status']=='pass' for r in results), failed=sum(r['status']=='fail' for r in results), opcodes=dict(opcodes), scope='Only data/data/stable/script/*.cpr .lub members; not every script elsewhere in the game.', members_detail=results)
(research/'evidence/startup/validation.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='members_detail'}, indent=2))
if summary['failed']:
    raise SystemExit(1)
