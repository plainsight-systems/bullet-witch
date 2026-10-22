#!/usr/bin/env python3
"""Hash-bound pass-06 native edges, tables, COM identities, exports and document links."""
from pathlib import Path
import struct,json,re,csv,collections,uuid
from engine_image import ROOT,META,raw,u32
out=ROOT/'evidence/engine/pass-06'
rejected={'00461785':'Interior call fragment; corrected to 00461770 in pass-06h.',
          '0043a400':'Interior instruction seed; corrected to 0043a3a0 in pass-06m.'}
def newest(kind,a):
 ps=sorted((ROOT/'evidence/engine').glob(f'pass-*/{kind}/{a:08x}.*'))
 return ps[-1] if ps else None
vt={0x6c2e64:{4:0x417220,0x10:0x417260,0x14:0x417290,0x18:0x415ca0},
    0x6c2e9c:{4:0x4173f0,0x10:0x417400,0x14:0x417440,0x18:0x415ca0},
    0x837c70:{0:0x4fbc00,4:0x4fbf80,8:0x4fc050,12:0x4fa030,16:0x4f7640},
    0x831f68:{0:0x4aca90,0x14:0x4acce0},0x829cd0:{0:0x43a490,4:0x43a290,8:0x43a280},
    0x829d68:{8:0x470c70,0x14:0x470ca0},0x827ff0:{0:0x427220},0x828008:{0:0x4277b0},
    0x828010:{0:0x4276d0},0x83caa0:{0:0x408270},0x6c20f4:{0:0x408f50}}
for task in json.loads((ROOT/'evidence/native-startup/mapping.json').read_text())['tasks']:
 vt[int(task['vtable_va'],16)]={int(k,16):int(v,16) for k,v in task['slots'].items()}
vt[0x841044]={0:0x5692d0}
vt[0x841f1c]={0:0x569970}
for a,slots in vt.items():
 for off,t in slots.items():assert u32(a+off)==t,(hex(a),off)
assert u32(0x6c0628)==0x401950 and 0x6c0544<=0x6c0628<0x6c0878
ids={0x6c0bc4:'5a508685-a254-4fba-9b82-9a24b00306af',0x6c0bd4:'8bcf1f58-9fe7-4583-8ac6-e2adc465c8bb',0x6c0b84:'bcc782bc-6492-4c22-8c35-f5d72fe73c6e',0x6c0b94:'b1ee676a-d9cd-4d2a-89a8-fa53eb9e480b',0x858fec:'e436ebb3-524f-11ce-9f53-0020af0ba770',0x85910c:'56a868a9-0ad4-11ce-b03a-0020af0ba770'}
for a,g in ids.items():assert str(uuid.UUID(bytes_le=raw(a,16)))==g
edges=[(0x411ee0,0x411f80),(0x43a290,0x43a3a0),(0x43a290,0x414f30),(0x43a290,0x40b670),(0x401950,0x410160),(0x5a7570,0x505310),(0x41b910,0x41b880),(0x415550,0x41b080),(0x41b080,0x41b270),(0x427220,0x430310),(0x430310,0x4303f0),(0x421e90,0x420280),(0x420280,0x4209b0),(0x562f00,0x562780),(0x562f70,0x562b00),(0x4acb30,0x507570),(0x50a2a0,0x507ff0),(0x4aca90,0x508670),(0x4c2830,0x4c2b80),(0x4711c0,0x4c2830),(0x43c520,0x4932c0),(0x43c520,0x493a20),(0x417440,0x40f920),(0x417290,0x41d340),(0x417290,0x419e20),(0x442c60,0x4acb30)]
edges += [(0x564460,0x563110),(0x564460,0x569620),(0x4ac9c0,0x569170),(0x569170,0x568cc0),(0x568cc0,0x568d20),(0x568cc0,0x40f1a0),(0x568d20,0x40f7f0),(0x5692d0,0x568f20),(0x499d00,0x562df0),(0x499f10,0x40b690)]
verified=[];missing=[]
for a,t in edges:
 p=newest('instructions',a);sites=[]
 if p:
  for addr,op,dest in re.findall(r'^([0-9a-f]{8})  (CALL|JMP) 0x([0-9a-f]{8})$',p.read_text(),re.M):
   if int(dest,16)!=t:continue
   addr=int(addr,16);b=raw(addr,5);assert b[0] in (0xe8,0xe9) and addr+5+struct.unpack('<i',b[1:])[0]==t
   sites.append({'address':hex(addr),'bytes':b.hex(),'operation':op})
 if not sites:missing.append([hex(a),hex(t)])
 else:verified.append({'source':hex(a),'target':hex(t),'sites':sites,'instructions':str(p.relative_to(ROOT))})
assert not missing,missing
status={};unique=set();unresolved=[]
for p in sorted((ROOT/'evidence/engine').glob('pass-06?/status.tsv')):
 rows=list(csv.DictReader(p.open(),delimiter='\t'));status[p.parent.name]=dict(collections.Counter(r['status'] for r in rows));assert all(r['status']=='ok' for r in rows)
 unique.update(r['address'] for r in rows if r['address'] not in rejected)
 unresolved.extend({'pass':p.parent.name,'line':x.strip(),'classification':'Rejected data seed; 0x5079b0 is jump-table data. Playback wrapper is 0x5079f0.'} for x in (p.parent/'console.log').read_text().splitlines() if 'UNRESOLVED ' in x)
docs=list((ROOT/'engine').rglob('*.md'))+[out/'ai/ACTION-INDEX.md',out/'FUNCTIONS.md',ROOT/'README.md',ROOT.parent/'BRIEF.md',ROOT/'reports/2026-10-22-engine-documentation-pass-06.md'];broken=[]
for p in docs:
 for dest in re.findall(r'(?<!!)\[[^\]]*\]\(([^)]+)\)',p.read_text()):
  if '://' in dest or dest.startswith('#'):continue
  if not (p.parent/dest.split('#')[0]).exists():broken.append({'file':str(p.relative_to(ROOT.parent)),'target':dest})
# Report/worklist may be created later; final invocation requires zero broken links.
result={'image_sha256':META['sha256'],'accepted_unique_exported_functions':len(unique),'export_status':status,'rejected_fragments':rejected,'rejected_seeds':unresolved,'vtable_slots':{hex(a):{hex(k):hex(v) for k,v in slots.items()} for a,slots in vt.items()},'crt_initializer':{'table_entry':'0x6c0628','target':'0x401950','range':['0x6c0544','0x6c0878']},'com_guids':{hex(a):v for a,v in ids.items()},'verified_edges':verified,'broken_engine_links':broken,'scope':'Static byte witnesses and research consistency; no original-game execution.'}
(out/'native-witnesses.json').write_text(json.dumps(result,indent=2)+'\n')
index=['# Pass 06 native evidence index','','Accepted exports; rejected interior fragments excluded. C signatures are inferred.','','| Address | Pseudocode | Instructions |','|---|---|---|']
for a in sorted(unique):
 addr=int(a,16);p=newest('functions',addr);ip=newest('instructions',addr)
 index.append(f'| 0x{addr:x} | [C](../{p.relative_to(ROOT/"evidence/engine")}) | [x86](../{ip.relative_to(ROOT/"evidence/engine")}) |')
(out/'FUNCTIONS.md').write_text('\n'.join(index)+'\n')
print(json.dumps({'accepted_unique_exported_functions':len(unique),'slices':len(status),'verified_edges':len(verified),'vtable_slots':sum(len(v) for v in vt.values()),'GUIDs':len(ids),'broken_links':broken},indent=2))

assert not broken,broken
