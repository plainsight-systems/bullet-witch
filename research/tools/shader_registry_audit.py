#!/usr/bin/env python3
"""Recover all named shader variants using native lookup layout, then validate token boundaries."""
from engine_image import ROOT,META,raw,u32,string
import struct,json,hashlib,collections
registry=0x954420;cache=0x9534d0
rows=[];used=set()
a=registry
while u32(a):
 name=string(u32(a));ids=struct.unpack('<4H',raw(a+4,8));row={'name':name,'record':hex(a),'S':{'vs':ids[0],'ps':ids[1]},'T':{'vs':ids[2],'ps':ids[3]}}
 rows.append(row);used.update(ids);a+=12
 assert len(rows)<2000
shaders=[]
for i in range((registry-cache)//8):
 p=u32(cache+i*8);version=u32(p);assert version in (0xfffe0300,0xffff0300)
 pos=p+4;instructions=comments=0
 while pos-p<1024*1024:
  token=u32(pos);op=token&65535
  if token==65535:break
  assert not token&0x80000000
  if op==0xfffe:length=(token>>16)&32767;comments+=1
  else:assert op<=96;length=(token>>24)&15;instructions+=1
  pos+=4*(1+length)
 else:raise AssertionError('unterminated shader')
 payload=raw(p,pos+4-p)
 shaders.append({'id':i,'pointer':hex(p),'profile':'vs_3_0' if version==0xfffe0300 else 'ps_3_0','bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),'instructions':instructions,'comments':comments,'initial_cache':hex(u32(cache+i*8+4))})
for row in rows:
 for variant in ['S','T']:
  for stage in ['vs','ps']:assert shaders[row[variant][stage]]['profile'].startswith(stage)
old=json.loads((ROOT/'evidence/layout/embedded-shader-candidates.json').read_text())
oldhashes={r['sha256'] for r in old};newhashes={r['sha256'] for r in shaders}
result={'image_sha256':META['sha256'],'name_table':hex(registry),'cache_table':hex(cache),'variant_count':len(rows),'cache_entries':len(shaders),'used_ids':len(used),'profiles':dict(collections.Counter(s['profile'] for s in shaders)),'unique_bytecodes':len(newhashes),'old_scan_candidates':len(old),'registry_hashes_in_old_scan':len(newhashes&oldhashes),'old_scan_hashes_outside_registry':len(oldhashes-newhashes),'variants':rows,'shaders':shaders,'scope':'All entries in this native registry. Token structure and stage assignments checked; no GPU execution.'}
out=ROOT/'evidence/engine/pass-06/shader-registry.json';out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ['variants','shaders']},indent=2))
