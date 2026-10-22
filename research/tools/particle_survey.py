#!/usr/bin/env python3
"""Read-only effect asset metadata and MSVC x86 RTTI survey. All output stays in research."""
from pathlib import Path
from collections import Counter
import json,re,struct,hashlib
r=Path(__file__).resolve().parents[1];out=r/'evidence/engine/pass-03';out.mkdir(parents=True,exist_ok=True)
meta=json.loads((r/'experiments/steamstub/validation.json').read_text())['unpacked'];b=Path(meta['path']).read_bytes()
assert hashlib.sha256(b).hexdigest()=='bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd'
def off(a):
 x=a-meta['image_base'];s=next(s for s in meta['sections'] if s['rva']<=x<s['rva']+s['file_bytes']);return s['file_offset']+x-s['rva']
def va(o):
 s=next(s for s in meta['sections'] if s['file_offset']<=o<s['file_offset']+s['file_bytes']);return meta['image_base']+s['rva']+o-s['file_offset']
def u(a):return struct.unpack_from('<I',b,off(a))[0]
def code(a):return 0x401000<=a<0x6c0000
refs={}
for s in meta['sections']:
 if s['rva']<0x2c0000:continue
 for o in range(s['file_offset'],s['file_offset']+s['file_bytes']-3,4):
  v=struct.unpack_from('<I',b,o)[0]
  if 0x400000<=v<0x2000000:refs.setdefault(v,[]).append(meta['image_base']+s['rva']+o-s['file_offset'])
classes=[]
for m in re.finditer(rb'\.\?AV[^\0\r\n]{3,200}\0',b):
 name=m.group()[:-1].decode('ascii','replace')
 if not re.search(r'Particle|Effect|Emitter',name,re.I):continue
 td=va(m.start())-8;vt=[]
 for ref in refs.get(td,[]):
  col=ref-12
  try:
   if u(col)!=0 or u(col+4)>0x10000 or u(col+8)>0x10000:continue
   for p in refs.get(col,[]):
    vtable=p+4
    if not code(u(vtable)):continue
    slots=[]
    for k in range(48):
     v=u(vtable+k*4)
     if not code(v):break
     slots.append(hex(v))
    vt.append({'locator':hex(col),'subobject_offset':u(col+4),'vtable':hex(vtable),'slots':slots})
  except (StopIteration,struct.error):pass
 classes.append({'name':name,'type_descriptor':hex(td),'vtable_candidates':vt})
(out/'particle-rtti.json').write_text(json.dumps({'sha256':meta['sha256'],'classes':classes,'scope':'Candidate tables from x86 MSVC RTTI structure and executable pointer bounds; class names are original. Method roles require code review.'},indent=2)+'\n')
game=Path('/Users/andyhunter/Games/BulletWitch-Steam');inventory=json.loads((r/'evidence/layout/cpr-inventory.json').read_text());found=[];errors=[];kinds=Counter();callnames=Counter()
for row in inventory:
 p=game/row['path']
 try:
  with p.open('rb') as f:
   h=f.read(96);assert h[:4]==b'CPR1';v=struct.unpack_from('<15I',h,4);table,aux,names,gap,size1=v[3:8];f.seek(96);records=f.read(table);ns=96+table+aux;f.seek(ns);strings=f.read(names);base=ns+names+gap;second=base+size1
   for i in range(0,table,48):
    kind=records[i:i+4].decode('ascii');a=struct.unpack_from('<11I',records,i+4);name=strings[a[0]:strings.index(b'\0',a[0])].decode('utf8','replace')
    if kind=='CALL':callnames[name]+=1
    if not (name.lower().endswith('.emt') or 'particle' in name.lower() or '/effect/' in row['path'].lower()):continue
    po,sz=(base+a[7],a[8]) if a[8] else ((second+a[9],a[10]) if a[10] else (None,0))
    if a[2]!=0xffffffff:po=0x28+a[2]
    if po is not None:
     assert po+sz<=p.stat().st_size
     f.seek(po)
    prefix=f.read(min(sz,48)) if po is not None else b'';kinds[kind]+=1
    found.append({'archive':row['path'],'index':i//48,'type':kind,'name':name,'payload_offset':po,'bytes':sz,'prefix_hex':prefix.hex()})
 except Exception as e:errors.append({'path':row['path'],'error':str(e)})
(out/'effect-assets.json').write_text(json.dumps({'archives_scanned':len(inventory),'errors':errors,'records':found,'record_types':dict(kinds),'all_call_names':dict(callnames)},indent=2)+'\n')
print(json.dumps({'classes':len(classes),'classes_with_vtable':sum(bool(x['vtable_candidates']) for x in classes),'archives_scanned':len(inventory),'effect_records':len(found),'emt_records':sum(x['name'].lower().endswith('.emt') for x in found),'errors':errors,'types':dict(kinds)},indent=2))
for c in classes:
 if c['vtable_candidates']:print(c['name'],[(x['vtable'],len(x['slots'])) for x in c['vtable_candidates']])
