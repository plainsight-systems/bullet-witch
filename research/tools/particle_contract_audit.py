#!/usr/bin/env python3
"""Reproduce particle contract data: curves, stage names, binding order, ABI slots.
No source game writes. Semantic names come from the native functions documented
in particle-runtime-contracts.md; raw records remain in earlier inventories.
"""
from pathlib import Path
from collections import Counter,defaultdict
import json,struct,re,hashlib
r=Path(__file__).resolve().parents[1];o=r/'evidence/engine/pass-04';o.mkdir(exist_ok=True)
m=json.loads((r/'experiments/steamstub/validation.json').read_text())['unpacked'];exe=Path(m['path']).read_bytes();assert hashlib.sha256(exe).hexdigest()==m['sha256']
def off(a):
 x=a-m['image_base'];s=next(s for s in m['sections'] if s['rva']<=x<s['rva']+s['file_bytes']);return s['file_offset']+x-s['rva']
def u(d,p):return struct.unpack_from('<I',d,p)[0]
def z(d,p):return d[p:d.index(b'\0',p)].decode('ascii')
constants={hex(a):{'u32':u(exe,off(a)),'f32':struct.unpack_from('<f',exe,off(a))[0]} for a in [0x82dc60,0x82cfb4,0x837360,0x83d3d4,0x829cc8,0x6c2e20,0x969100]}
rows=json.loads((r/'evidence/engine/pass-03/effect-assets.json').read_text())['records'];packs={};stages=[]
for row in rows:
 if row['name'] not in ('effect_pack.bin','effect.emt'):continue
 with (Path('/Users/andyhunter/Games/BulletWitch-Steam')/row['archive']).open('rb') as f:f.seek(row['payload_offset']);d=f.read(row['bytes'])
 if row['name']=='effect.emt':
  block=16+u(d,16);n=u(d,block);t=block+4+u(d,block+4);indices=t+u(d,t);strings=t+4+u(d,t+4);names=[z(d,strings+struct.unpack_from('<H',d,indices+2*i)[0]) for i in range(n)]
  meta=block+12+u(d,block+12);entry_meta=[list(struct.unpack_from('<3I',d,meta+i*12)) for i in range(n)]
  stages.append({'archive':row['archive'],'version_words':list(struct.unpack_from('<2H',d,4)),'entries':[{'index':i,'name':name,'meta_u32':entry_meta[i]} for i,name in enumerate(names)]});continue
 offsets=[u(d,12+4*i) for i in range(7)];counts=[u(d,p+8) if p else 0 for p in offsets[:5]]
 for index,stride in [(3,0x84),(4,0x180)]:assert offsets[index]+16+counts[index]*stride<=len(d)
 curves=[]
 for i in range(counts[3]):
  p=offsets[3]+16+i*0x84;mode=u(d,p);assert mode<=16
  curves.append({'index':i,'mode_or_key_count':mode,'values':list(struct.unpack_from('<32f',d,p+4))})
 defs=[]
 for i in range(counts[2]):
  p=offsets[2]+16+i*0xd4;refs={hex(k):struct.unpack_from('<i',d,p+k)[0] for k in [0xa4,0xa8,0xac]}
  for k,idx in refs.items():assert idx<0 or idx<counts[4 if k=='0xa4' else 3]
  defs.append({'index':i,'references':refs})
 ids=[u(d,offsets[0]+16+i*0xb0) for i in range(counts[0])]
 packs[Path(row['archive']).stem]={'curve_count':counts[3],'vector_table_count':counts[4],'scalar_modes':dict(Counter(c['mode_or_key_count'] for c in curves)),'curves':curves,'definition_references':defs,'stored_ids':ids,'section_5_present':bool(offsets[5]),'section_6_header':list(struct.unpack_from('<3I',d,offsets[6]))}
order=['effect_com','effect_com2','effect_com3','effect_boss1'];bindings=defaultdict(list)
for slot,name in enumerate(order):
 for idx,ident in enumerate(packs[name]['stored_ids']):bindings[ident].append({'pack':name,'slot':slot,'index':idx})
header=(r/'vendor/d3d9-reference/d3d9.h').read_text();body=header.split('DECLARE_INTERFACE_IID_(IDirect3DDevice9,IUnknown',1)[1].split('};',1)[0]
methods=re.findall(r'STDMETHOD\((\w+)\)|STDMETHOD_\([^,]+,\s*(\w+)\)',body);methods=[a or b for a,b in methods];assert methods[81]=='DrawPrimitive' and methods[57]=='SetRenderState'
abi={hex(i*4):name for i,name in enumerate(methods)}
vt=json.loads((r/'evidence/engine/pass-03/particle-rtti.json').read_text())['classes'];catalog=[]
for c in vt:
 if 'cgl@@' not in c['name']:continue
 for v in c['vtable_candidates']:
  entries=[]
  for i,a in enumerate(v['slots']):
   matches=[str(p.relative_to(r)) for p in (r/'evidence/engine/pass-04a/functions').glob(f'{int(a,16):08x}.c')]
   entries.append({'byte_offset':hex(i*4),'address':a,'export':matches[0] if matches else None})
  catalog.append({'name':c['name'],'vtable':v['vtable'],'methods':entries})
result={'constants':constants,'packs':packs,'stages':stages,'duplicate_binding_sequences':{str(k):v for k,v in bindings.items() if len(v)>1},'binding_assumption':'0x6084f0 order, all four archives available, ID adjustment zero; later writes win. Absent archives skipped.','d3d9_x86_vtable':abi,'class_method_catalog':catalog}
# SM3 instruction lengths include operands, excluding the instruction token.
shader_addresses=[0x83aab8,0x83acf8,0x83adc0,0x83b108,0x83b430,0x83b688,0x83b890,0x83bb10,0x83bd70,0x83c008,0x83c350,0x83c590,0x83c680,0x83c8d8]
shaders=[]
for a in shader_addresses:
 start=off(a);p=start+4;version=u(exe,start);assert version in (0xfffe0300,0xffff0300);n=0
 while True:
  token=u(exe,p);opcode=token&0xffff
  if opcode==0xffff:p+=4;break
  length=((token>>16)&0x7fff) if opcode==0xfffe else ((token>>24)&15)
  assert length>0 and p-start<0x1000
  p+=4*(1+length);n+=1
 shaders.append({'address':hex(a),'version':hex(version),'bytes':p-start,'instructions_and_comments':n,'sha256':hashlib.sha256(exe[start:p]).hexdigest()})
(o/'shader-manifest.json').write_text(json.dumps(shaders,indent=2)+'\n')
assert all(e['meta_u32'][1] in bindings for stage in stages for e in stage['entries'])
(o/'contract-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'constants':constants,'pack_curves':{n:[p['curve_count'],p['vector_table_count'],p['scalar_modes']] for n,p in packs.items()},'stage_names':[s['entries'] for s in stages],'duplicate_bindings':result['duplicate_binding_sequences'],'d3d_method_count':len(methods)},indent=2))
