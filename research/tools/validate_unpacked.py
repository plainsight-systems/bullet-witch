#!/usr/bin/env python3
"""Independent structural comparison of the offline-unpacked research copy.

Checks PE ranges/alignment, entry point, unchanged non-code sections/directories,
and exact x86 adapter shapes at all previously located Lua handler candidates.
Does not load or execute the executable.
"""
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

research = Path(__file__).resolve().parents[1]
work = research/'experiments/steamstub'
source = Path('/Users/andyhunter/Games/BulletWitch-Steam/witch.exe')
output = work/'witch.original.exe.unpacked.exe'

def sha(data): return hashlib.sha256(data).hexdigest()

def parse(path):
    data = path.read_bytes()
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    assert u16(pe+4) == 0x14c and u16(pe+24) == 0x10b
    opt = pe+24
    file_align, section_align = u32(opt+36), u32(opt+32)
    sections = []
    for n in range(u16(pe+6)):
        off = opt+u16(pe+20)+40*n
        vsize, rva, size, raw = struct.unpack_from('<4I', data, off+8)
        assert raw+size <= len(data) and raw % file_align == 0 and rva % section_align == 0
        blob = data[raw:raw+size]
        entropy = -sum((n/size)*math.log2(n/size) for n in Counter(blob).values()) if size else 0
        sections.append(dict(name=data[off:off+8].rstrip(b'\0').decode(), rva=rva, virtual_size=vsize, file_offset=raw, file_bytes=size, characteristics=u32(off+36), sha256=sha(blob), entropy=entropy))
    for a, b in zip(sorted(sections, key=lambda s:s['rva']), sorted(sections, key=lambda s:s['rva'])[1:]):
        assert a['rva']+a['virtual_size'] <= b['rva']
    for a, b in zip(sorted(sections, key=lambda s:s['file_offset']), sorted(sections, key=lambda s:s['file_offset'])[1:]):
        assert a['file_offset']+a['file_bytes'] <= b['file_offset']
    assert all(s['rva']+s['virtual_size'] <= u32(opt+56) for s in sections)
    dirs = [struct.unpack_from('<II', data, opt+96+8*n) for n in range(u32(opt+92))]
    return dict(path=str(path), bytes=len(data), sha256=sha(data), image_base=u32(opt+28), entry_rva=u32(opt+16), size_of_image=u32(opt+56), checksum=u32(opt+64), directories=dirs, sections=sections), data

old, original = parse(source)
new, unpacked = parse(output)
assert sha((work/'witch.original.exe').read_bytes()) == old['sha256']
assert old['sha256'] == 'f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc'
assert new['entry_rva'] == int(json.loads((work/'unpack-result.json').read_text())['original_entry_rva'], 16)
assert old['image_base'] == new['image_base']
assert old['directories'] == new['directories']
assert '.bind' not in [s['name'] for s in new['sections']]
unchanged = []
for s in new['sections']:
    before = next(x for x in old['sections'] if x['name']==s['name'])
    for k in ['rva', 'virtual_size', 'file_offset', 'file_bytes', 'characteristics']:
        assert before[k] == s[k], (s['name'], k)
    if s['name'] != '.text':
        assert s['sha256'] == before['sha256']
        unchanged.append(s['name'])
assert any(s['rva'] <= new['entry_rva'] < s['rva']+s['virtual_size'] and s['characteristics'] & 0x20000000 for s in new['sections'])

def offset(va, size=1):
    rva = va-new['image_base']
    s = next(s for s in new['sections'] if s['rva'] <= rva and rva+size <= s['rva']+s['file_bytes'])
    return s['file_offset']+rva-s['rva']

def executable(va):
    rva = va-new['image_base']
    return any(s['rva'] <= rva < s['rva']+s['virtual_size'] and s['characteristics']&0x20000000 for s in new['sections'])

mapping = json.loads((research/'evidence/startup/native-map.json').read_text())
handlers = []
for symbol in mapping['symbols']:
    candidates = {int(c['handler_va'],16) for loc in symbol['locations'] for c in loc['candidates']}
    for va in sorted(candidates):
        raw = offset(va,18)
        code = unpacked[raw:raw+18]
        # push imm32; push [esp+8]; call rel32; add esp,8; ret
        matches = code[0]==0x68 and code[5:10]==bytes.fromhex('ff742408e8') and code[14:18]==bytes.fromhex('83c408c3')
        row = dict(symbol=symbol['symbol'], adapter_va=hex(va), adapter_hex=code.hex(), matches_adapter_shape=matches)
        if matches:
            target = struct.unpack_from('<I',code,1)[0]
            bridge = va+14+struct.unpack_from('<i',code,10)[0]
            assert executable(target) and executable(bridge)
            row.update(function_argument_va=hex(target), bridge_va=hex(bridge))
        handlers.append(row)
summary = dict(original=old, unpacked=new, unchanged_noncode_sections=unchanged,
               directories_unchanged=True, source_and_input_hashes_unchanged=True,
               handler_candidates=len(handlers), adapter_shape_matches=sum(r['matches_adapter_shape'] for r in handlers), handlers=handlers,
               limitation='Static structure and adapter checks only. Runtime startup, Steam independence and full code semantics are not tested.')
(work/'validation.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(dict(original_bytes=old['bytes'], unpacked_bytes=new['bytes'], entry_va=hex(new['image_base']+new['entry_rva']), original_text_entropy=old['sections'][0]['entropy'], unpacked_text_entropy=new['sections'][0]['entropy'], unchanged_noncode_sections=unchanged, candidates=len(handlers), matches=summary['adapter_shape_matches']),indent=2))
