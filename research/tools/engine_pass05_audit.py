#!/usr/bin/env python3
"""Reproduce actor lifecycle vtable/call/RTTI witnesses and evidence navigation."""
from pathlib import Path
import struct,json,hashlib,re,collections
r=Path(__file__).resolve().parents[1];o=r/'evidence/engine/pass-05';o.mkdir(exist_ok=True)
m=json.loads((r/'experiments/steamstub/validation.json').read_text())['unpacked'];d=Path(m['path']).read_bytes();assert hashlib.sha256(d).hexdigest()==m['sha256']
def raw(a,n):
 x=a-m['image_base'];s=next(s for s in m['sections'] if s['rva']<=x<s['rva']+s['file_bytes']);p=s['file_offset']+x-s['rva'];return d[p:p+n]
def word(a):return struct.unpack('<I',raw(a,4))[0]
def newest(kind,a):
 paths=sorted((r/'evidence/engine').glob(f'pass-05*/{kind}/{a:08x}.*'),reverse=True)
 if not paths:paths=sorted((r/'evidence/engine').glob(f'pass-*/{kind}/{a:08x}.*'),reverse=True)
 return paths[0] if paths else None
expected={0x852334:{0:0x5feef0,8:0x602b70,0x28:0x602a30,0x2c:0x602aa0},0x850d9c:{0:0x5e1620,8:0x5e37f0,0xc:0x5e3560,0x28:0x5e5280,0x2c:0x5e5220},0x840864:{4:0x568960},0x847c9c:{4:0x5a7570},0x82f3f4:{8:0x56b610},0x8523c0:{8:0x45f1f0},0x82c6b4:{0xa4:0x450c70},0x82cdac:{0x1c:0x5adc60},0x82cd8c:{0x1c:0x5adc80}}
for v,slots in expected.items():
 for offset,target in slots.items():assert word(v+offset)==target,(hex(v),hex(offset))
edges=[(0x45f070,t) for t in [0x568bd0,0x44f650,0x561470,0x6048f0,0x5ffa30,0x608890]]+[(0x44f650,0x45069c),(0x44f650,0x44f650),(0x602b70,0x65a570),(0x65a570,0x568930),(0x568930,0x45f1f0),(0x5e37f0,0x56b850),(0x5e37f0,0x5f9b10),(0x5e5220,0x423d20),(0x5e5220,0x423b40),(0x56b610,0x56ab10),(0x5feef0,0x5fede0),(0x5fede0,0x6583e0),(0x6583e0,0x658350),(0x658350,0x568600),(0x658350,0x5611d0),(0x44dc00,0x5a5080),(0x5a7570,0x503210),(0x5a7570,0x505310),(0x445580,0x4806e0),(0x445580,0x561370)]
checked=[]
for source,target in edges:
 p=newest('instructions',source);assert p,(hex(source),hex(target))
 sites=[]
 for a,op,dest in re.findall(r'^([0-9a-f]{8})  (CALL|JMP) 0x([0-9a-f]{8})$',p.read_text(),re.M):
  if int(dest,16)!=target:continue
  a=int(a,16);b=raw(a,5);assert b[0] in (0xe8,0xe9);assert a+5+struct.unpack('<i',b[1:])[0]==target
  sites.append({'address':hex(a),'bytes':b.hex(),'operation':op})
 assert sites,(hex(source),hex(target));checked.append({'source':hex(source),'target':hex(target),'sites':sites,'instructions':str(p.relative_to(r))})
for a,b in [(0x45069c,'8b01ff6008'),(0x4506a1,'8b01ff600c')]:assert raw(a,len(bytes.fromhex(b)))==bytes.fromhex(b)
types={0x97ce20:'.?AVAiBehavior@witch@@',0x97d3ac:'.?AUStateStart@witch@@',0x97d4ac:'.?AVRunner@witch@@',0x97d054:'.?AVShooter@witch@@',0x97ce60:'.?AVGrappler@witch@@',0x97d508:'.?AVAiGrappleCapacity@witch@@',0x97d6b4:'.?AVDodger@witch@@'}
for a,name in types.items():assert raw(a+8,len(name)+1)==name.encode()+b'\0'
statuses={};unique=set();unresolved=[]
for p in sorted((r/'evidence/engine').glob('pass-05?/status.tsv')):
 lines=p.read_text().splitlines()[1:];statuses[p.parent.name]=dict(collections.Counter(l.split('\t')[-1] for l in lines));unique.update(l.split('\t')[0] for l in lines)
 assert all(l.endswith('\tok') for l in lines),p
 log=p.parent/'console.log'
 unresolved.extend({'pass':p.parent.name,'line':line} for line in log.read_text().splitlines() if 'UNRESOLVED ' in line)
result={'image_sha256':m['sha256'],'vtable_slots':{hex(a):{hex(k):hex(v) for k,v in slots.items()} for a,slots in expected.items()},'verified_edges':checked,'verified_thunks':['0x45069c','0x4506a1'],'original_rtti':{hex(a):name for a,name in types.items()},'export_status':statuses,'unique_functions':len(unique),'unresolved_export_seeds':unresolved}
(o/'native-witnesses.json').write_text(json.dumps(result,indent=2)+'\n')
addresses={int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{6,8})',(r/'engine/subsystems/actor-lifecycle.md').read_text())}
index=['# Actor lifecycle native function index','','Pseudocode types/signatures are inferred. Use adjacent instructions for calling conventions.','','| VA | C | x86 instructions |','|---|---|---|']
missing=[]
for a in sorted(addresses):
 p=newest('functions',a)
 if not p:continue
 rel=p.relative_to(r/'evidence/engine');ins=rel.parent.parent/'instructions'/f'{a:08x}.txt'
 index.append(f'| 0x{a:x} | [C](../{rel}) | [x86](../{ins}) |')
(o/'FUNCTIONS.md').write_text('\n'.join(index)+'\n')
print(json.dumps({'export_status':statuses,'unique_functions':len(unique),'verified_edges':len(checked),'vtable_slots':sum(map(len,expected.values())),'rtti':len(types),'unresolved_export_seeds':unresolved},indent=2))
