#!/usr/bin/env python3
"""Inventory effect definitions using native 0x50ed10/0x50eb30/0x50f900 layouts.
Reads originals only. Bounds and cross-table indices are checked; unknown fields
are retained without assigning semantics. This does not simulate an effect.
"""
from pathlib import Path
from local_inputs import game_directory
from collections import Counter
import hashlib,json,struct
from cpr_layout import inspect
research=Path(__file__).resolve().parents[1]
out=research/'evidence/engine/pass-03'
game=game_directory()
rows=json.loads((out/'effect-assets.json').read_text())['records']
def u(d,o):return struct.unpack_from('<I',d,o)[0]
def signed(d,o):return struct.unpack_from('<i',d,o)[0]
result={'layout_witnesses':['0x50ed10','0x50eb30','0x50f900','0x50c520','0x50aa60'],'packs':[],'stage_tables':[]}
for row in rows:
 if row['name'] not in ('effect_pack.bin','effect.emt'):continue
 p=game/row['archive'];cp=inspect(p);rec=cp['records'][row['index']]
 assert (rec['payload_offset'],rec['payload_bytes'])==(row['payload_offset'],row['bytes'])
 with p.open('rb') as f:f.seek(rec['payload_offset']);d=f.read(rec['payload_bytes'])
 common={'archive':row['archive'],'archive_sha256':cp['sha256'],'member':row['name'],'bytes':len(d),'payload_sha256':hashlib.sha256(d).hexdigest()}
 if row['name']=='effect.emt':
  assert d[:4]==b'EVMT'
  pointers={hex(o):o+u(d,o) if u(d,o) else None for o in (8,16,24,32)}
  assert all(v is None or 0<=v<len(d) for v in pointers.values())
  result['stage_tables'].append({**common,'field_relative_targets':pointers,'entry_count':u(d,pointers['0x10'])})
  continue
 assert d[:4]==b'KPKd' and u(d,8)==0x9e and u(d,4)==7
 offsets=[u(d,12+4*i) for i in range(7)]
 sections=[]
 for i,o in enumerate(offsets):
  if not o:sections.append(None);continue
  assert o+12<=len(d)
  sections.append({'index':i,'offset':o,'signature':d[o:o+4].decode('ascii'),'header_words':list(struct.unpack_from('<3I',d,o))})
 counts=[u(d,o+8) for o in offsets[:3]]
 for o,n,stride in zip(offsets,counts,(0xb0,0x210,0xd4)):
  assert o+16+n*stride<=len(d)
 types=[u(d,offsets[2]+16+i*0xd4) for i in range(counts[2])]
 assert set(types)<=set(range(6))
 parts=[]
 for i in range(counts[1]):
  o=offsets[1]+16+i*0x210;j=signed(d,o+4);assert 0<=j<counts[2]
  parts.append({'index':i,'definition_index':j,'type_code':types[j],'capacity_field_0x24':u(d,o+0x24)})
 effects=[]
 for i in range(counts[0]):
  o=offsets[0]+16+i*0xb0;refs=list(struct.unpack_from('<16i',d,o+4));assert all(j<0 or j<counts[1] for j in refs)
  effects.append({'index':i,'effect_id':u(d,o),'part_indices':refs,'type_codes':sorted({parts[j]['type_code'] for j in refs if j>=0})})
 result['packs'].append({**common,'sections':sections,'effect_count':counts[0],'part_count':counts[1],'definition_count':counts[2],'definition_types':dict(Counter(types)),'model_name_fields':sorted({d[offsets[2]+16+i*0xd4+0x10:offsets[2]+16+i*0xd4+0x20].split(b'\0')[0].decode('ascii','replace') for i,t in enumerate(types) if t==3}),'effects':effects,'parts':parts})
result['summary']={'packs':len(result['packs']),'effects':sum(p['effect_count'] for p in result['packs']),'parts':sum(p['part_count'] for p in result['packs']),'definitions':sum(p['definition_count'] for p in result['packs']),'stage_tables':len(result['stage_tables']),'stage_entries':sum(p['entry_count'] for p in result['stage_tables'])}
ids=Counter(e['effect_id'] for p in result['packs'] for e in p['effects']);result['summary']['unique_stored_effect_ids']=len(ids);result['summary']['repeated_stored_ids']=[i for i,n in ids.items() if n>1]
(out/'effect-pack-inventory.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result['summary'],indent=2))
for p in result['packs']:print(p['archive'],p['effect_count'],p['part_count'],p['definition_count'],p['definition_types'])
