#!/usr/bin/env python3
"""Read-only PE evidence: section entropy and candidate Lua name/handler pairs.

Candidates are data pointers to a named string followed by a pointer into an
executable section. They do not prove registration or executable handler logic.
"""
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

research = Path(__file__).resolve().parents[1]
exe = Path('/Users/andyhunter/Games/BulletWitch-Steam/witch.exe')
b = exe.read_bytes()
u16 = lambda off: struct.unpack_from('<H', b, off)[0]
u32 = lambda off: struct.unpack_from('<I', b, off)[0]
pe = u32(0x3c)
assert b[pe:pe+4] == b'PE\0\0' and u16(pe+4) == 0x14c
opt = pe+24
assert u16(opt) == 0x10b
base = u32(opt+28)
entry = base+u32(opt+16)
sections = []
for i in range(u16(pe+6)):
    off = opt+u16(pe+20)+40*i
    name = b[off:off+8].rstrip(b'\0').decode('ascii')
    virtual_size, rva, size, raw = struct.unpack_from('<4I', b, off+8)
    assert raw+size <= len(b)
    blob = b[raw:raw+size]
    counts = Counter(blob)
    entropy = -sum((n/size)*math.log2(n/size) for n in counts.values()) if size else 0
    sections.append(dict(name=name, va=base+rva, virtual_size=virtual_size,
                         file_offset=raw, file_bytes=size,
                         characteristics=hex(u32(off+36)),
                         executable=bool(u32(off+36)&0x20000000),
                         entropy_bits_per_byte=entropy))

def at_va(va):
    return next((s for s in sections if s['va'] <= va < s['va']+s['virtual_size']), None)

def file_va(off):
    s = next((s for s in sections if s['file_offset'] <= off < s['file_offset']+s['file_bytes']), None)
    return s['va']+off-s['file_offset'] if s else None

def walk(f):
    yield f
    for child in f['children']:
        yield from walk(child)

refs = {}
for name in ['root', 'root_riff', 'default_callback']:
    root = json.loads((research/f'evidence/startup/{name}.lub.json').read_text())['root']
    for f in walk(root):
        for ins in f['code']:
            if ins['op'] == 'GETGLOBAL' and ins['constant'].startswith('_'):
                refs.setdefault(ins['constant'], []).append(dict(script=name, function=f.get('global_name', f['id']), pc=ins['pc']))
symbols = []
for symbol, uses in sorted(refs.items()):
    needle = symbol.encode()+b'\0'
    locations = []
    pos = b.find(needle)
    while pos >= 0:
        va = file_va(pos)
        candidates = []
        if va is not None:
            pointer = struct.pack('<I', va)
            for s in sections:
                if s['executable']:
                    continue
                start, end = s['file_offset'], s['file_offset']+s['file_bytes']
                ref = b.find(pointer, start, end)
                while ref >= 0:
                    if ref % 4 == 0 and ref+8 <= end:
                        handler = u32(ref+4)
                        target = at_va(handler)
                        if target and target['executable']:
                            delta = handler-target['va']
                            raw = target['file_offset']+delta if delta < target['file_bytes'] else None
                            candidates.append(dict(table_file_offset=hex(ref), table_va=hex(file_va(ref)), handler_va=hex(handler), handler_file_offset=hex(raw) if raw is not None else None, handler_section=target['name']))
                    ref = b.find(pointer, ref+1, end)
        locations.append(dict(string_file_offset=hex(pos), string_va=hex(va) if va is not None else None, candidates=candidates))
        pos = b.find(needle, pos+1)
    symbols.append(dict(symbol=symbol, uses=uses, locations=locations))
result = dict(executable=str(exe), sha256=hashlib.sha256(b).hexdigest(), image_base=hex(base), entry_va=hex(entry), entry_section=at_va(entry)['name'], sections=sections, symbols=symbols,
              caveat='Static candidate pointer pairs only; native registration, loader transformation and handler behavior have not been executed or verified.')
(research/'evidence/startup/native-map.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(dict(entry_va=hex(entry), entry_section=at_va(entry)['name'], sections=sections, symbols=len(symbols), symbols_with_candidates=sum(any(loc['candidates'] for loc in row['locations']) for row in symbols)), indent=2))
