#!/usr/bin/env python3
"""Verify selected effect/actor table and x86 call witnesses against image bytes."""
from pathlib import Path
import hashlib,json,struct
r=Path(__file__).resolve().parents[1];out=r/'evidence/engine/pass-03'
m=json.loads((r/'experiments/steamstub/validation.json').read_text())['unpacked'];d=Path(m['path']).read_bytes();sha=hashlib.sha256(d).hexdigest()
assert sha=='bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd'
def off(a):
 x=a-m['image_base'];s=next(s for s in m['sections'] if s['rva']<=x<s['rva']+s['file_bytes']);return s['file_offset']+x-s['rva']
def u(a):return struct.unpack_from('<I',d,off(a))[0]
def st(a):o=off(a);return d[o:d.index(b'\0',o)].decode('ascii')
a={'effect_pack_names_0x976ca4':[st(u(0x976ca4+4*i)) for i in range(4)],'actor_kind_names':{str(i+1):st(v) for i,v in enumerate([0x82be20,0x82be1c,0x8290c8,0x82c8e8,0x82c914,0x82c8e4,0x82c120])},'stage_effect_vtable_0x837770':[hex(u(0x837770+4*i)) for i in range(4)]}
(out/'native-tables.json').write_text(json.dumps(a,indent=2)+'\n')
assert u(0x837770)==0x4dbee0
assert u(0x83cbf8+0xc)==0x52b8a0
assert u(0x83cbf8+0x20)==0x516000
assert d[off(0x52e487):off(0x52e487)+3]==bytes.fromhex('ff520c')
calls=[]
for site,target in [(0x608507,0x50eca0),(0x608536,0x52fbe0),(0x45f1ce,0x608890),(0x6088a1,0x52e8e0),(0x510c2f,0x50f900),(0x4dbf21,0x510b50),(0x4ca39f,0x4dbeb0),(0x4ca68a,0x4dbd10)]:
 o=off(site);assert d[o]==0xe8;actual=site+5+struct.unpack_from('<i',d,o+1)[0];assert actual==target
 calls.append({'site':hex(site),'target':hex(actual),'bytes':d[o:o+5].hex()})
rows=[];statuses={}
for part in ('pass-03a','pass-03b','pass-03c'):
 values=[s.split('\t') for s in (r/'evidence/engine'/part/'status.tsv').read_text().splitlines()[1:]]
 assert all(v[-1]=='ok' for v in values);statuses[part]=len(values);rows+=values
result={'sha256':sha,'verified_direct_calls':calls,'verified_sprite_vtable':{'simulation_slot_0x0c':'0x52b8a0','geometry_slot_0x20':'0x516000'},'verified_dispatch_site':'0x52e487: CALL [EDX+0xc]','exports':statuses,'distinct_exported_addresses':len({v[0] for v in rows}),'scope':'Static byte witnesses. No game execution.'}
(out/'native-witnesses.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
