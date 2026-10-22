#!/usr/bin/env python3
"""Hash-bound instruction/table witnesses and linked export index for engine pass 04."""
from pathlib import Path
import json,struct,hashlib,re,collections
r=Path(__file__).resolve().parents[1];o=r/'evidence/engine/pass-04'
m=json.loads((r/'experiments/steamstub/validation.json').read_text())['unpacked'];d=Path(m['path']).read_bytes();assert hashlib.sha256(d).hexdigest()==m['sha256']
def at(a,n):
 x=a-m['image_base'];s=next(s for s in m['sections'] if s['rva']<=x<s['rva']+s['file_bytes']);p=s['file_offset']+x-s['rva'];return d[p:p+n]
def word(a):return struct.unpack('<I',at(a,4))[0]
def check(a,b,meaning):
 raw=bytes.fromhex(b);assert at(a,len(raw))==raw,(hex(a),at(a,len(raw)).hex());return {'address':hex(a),'bytes':b,'meaning':meaning}
witnesses=[check(0x5134bb,'ff501c','part sorting/fade virtual +0x1c'),check(0x5134e9,'ff5220','part geometry virtual +0x20'),check(0x5134fb,'ff5020','alternate geometry call +0x20'),check(0x53dd50,'ff9244010000','D3D9 DrawPrimitive +0x144'),check(0x53dbd2,'ff9190010000','D3D9 SetStreamSource +0x190'),check(0x44fdfd,'c706b4c68200','base game constructor vtable write'),check(0x4589cf,'8935d4340f01','active game owner publication')]
assert at(0x43b907,1)==b'\xe8' and 0x43b907+5+struct.unpack('<i',at(0x43b908,4))[0]==0x4589c0
vts=[0x82c6b4,0x82d130,0x829e18,0x82dc80,0x828fc0,0x82dd78,0x82dfb0,0x82e068,0x82e120]
expected=[0x4563e0,0x4562b0,0x4566b0,0x456930,0x456140,0x456820,0x456130,0x455fb0]
tables={hex(v):{hex(k):hex(word(v+k)) for k in range(0x48,0x68,4)} for v in vts}
assert all([word(v+k) for k in range(0x48,0x68,4)]==expected for v in vts)
assert word(0x83cae8)==0x53dad0
statuses={};unique=set()
for name in ['04a','04b','04c']:
 lines=(r/f'evidence/engine/pass-{name}/status.tsv').read_text().splitlines()[1:];statuses[name]=dict(collections.Counter(l.split('\t')[-1] for l in lines));unique.update(l.split('\t')[0] for l in lines);assert all(l.endswith('\tok') for l in lines)
# Link every native function address mentioned by either current subsystem document.
addresses=set()
for name in ['particle-runtime-contracts.md','actors-and-animation.md']:
 addresses.update(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{6,8})',(r/'engine/subsystems'/name).read_text()))
index=['# Pass 04 native function index','','Pseudocode has inferred types. Adjacent instructions and source image bytes take precedence.','','| VA | Pseudocode | Instructions |','|---|---|---|']
for a in sorted(addresses):
 matches=sorted((r/'evidence/engine').glob(f'pass-*/functions/{a:08x}.c'),reverse=True)
 if not matches:continue
 p=matches[0];relative=p.relative_to(r/'evidence/engine');ins=relative.parent.parent/'instructions'/f'{a:08x}.txt'
 index.append(f'| 0x{a:x} | [C](../{relative}) | [x86](../{ins}) |')
(o/'FUNCTIONS.md').write_text('\n'.join(index)+'\n')
result={'image_sha256':m['sha256'],'instruction_witnesses':witnesses,'game_factory_vtables':tables,'effect_command_executor':hex(word(0x83cae8)),'export_status':statuses,'unique_exported_functions':len(unique)}
(o/'native-witnesses.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'status':statuses,'unique_functions':len(unique),'game_vtables':len(tables),'instruction_witnesses':len(witnesses)}))
