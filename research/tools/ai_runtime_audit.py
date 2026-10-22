#!/usr/bin/env python3
"""Validate AI traversal/data relationships using the native getter formulas."""
from pathlib import Path
import json,struct,collections,hashlib
R=Path(__file__).resolve().parents[1];O=R/'evidence/engine/pass-06/ai';O.mkdir(parents=True,exist_ok=True)
def u(b,p):return struct.unpack_from('<I',b,p)[0]
def s(b,p):return struct.unpack_from('<i',b,p)[0]
def chunks(b,start,end):
 out={};p=start
 while p<end:
  n=u(b,p+4);assert p+n+8<=end;out[b[p:p+4]]=(p,n);p+=n+8
 assert p==end;return out
def refs(b,t):
 p,n=t;cs=chunks(b,p+8,p+8+n);root=cs[b'data'][0];rp,rn=cs[b'refd'];return root,[u(b,rp+8+i) for i in range(0,rn,4)]
from engine_image import u32,string,META
registry=[{'code':i,'factory':hex(u32(0x846fb8+i*12)),'name':string(u32(0x846fbc+i*12)),'storage_bytes':u32(0x846fc0+i*12)} for i in range(0x51c//12)]
(O/'action-factories.json').write_text(json.dumps({'image_sha256':META['sha256'],'table':'0x846fb8','records':registry},indent=2)+'\n')
byname={x['name']:x for x in registry}
summary=[];actions=collections.Counter();filters=collections.Counter();modes=collections.Counter();bad=[]
for path in sorted((R/'evidence/script-corpus/ai-nodes').glob('*.json')):
 old=json.loads(path.read_text());b=(R/'extracted/ai-wrappers'/old['wrapper']).read_bytes();tags=chunks(b,8,len(b));base=old['node_base_in_wrapper'];nodes=old['nodes'];N=len(nodes)*12
 dr,ds=refs(b,tags[b'data']);sr,ss=refs(b,tags[b'str\0']);out=[]
 for i,n in enumerate(nodes):
  off=n['offset'];span,di,back=n['u32_fields'];assert di<len(ds)
  end=off+span+4;assert span>=8 and (span+4)%12==0 and end<=N
  parent=None if i==0 else off-back
  if parent is not None:assert parent>=0 and parent%12==0 and parent<off and end<=parent+nodes[parent//12]['u32_fields'][0]+4
  first=off+12 if span>16 else None
  sibling=end if parent is not None and end<parent+nodes[parent//12]['u32_fields'][0]+4 else None
  d=dr+8+ds[di];a=s(b,d+8);f=s(b,d+12);m=s(b,d+16);priority=struct.unpack_from('<h',b,d+20)[0];stringidx=s(b,d+24)
  assert b[d+28:d+32]==b'eval';namelen=u(b,d+32);name=b[d+36:d+36+namelen].split(b'\0')[0].decode();assert name==n['lua_function']
  ps=d+36+namelen;pn=u(b,ps);assert ps+4+pn<=len(b)
  label=None
  if 0<=stringidx<len(ss):
   q=sr+8+ss[stringidx];label=b[q:b.index(b'\0',q)].decode('utf8',errors='replace')
  row={'offset':off,'subtree_end_exclusive':end,'parent':parent,'first_child':first,'next_sibling':sibling,'data_index':di,'data_record_offset':d,'action_code':a,'eligibility_mode':f,'evaluation_mode':m,'priority_i16':priority,'name_string_index':stringidx,'name':label,'lua_function':name,'parameter_bytes':pn,'parameter_hex':b[ps+4:ps+4+pn].hex()}
  row['named_pc_action']=byname.get(label)
  out.append(row);actions[a]+=1;filters[f]+=1;modes[m]+=1
 entrypoints=[v+4 for v in old['call_offsets']];assert all(0<=v<N and v%12==0 for v in entrypoints)
 result={'wrapper':old['wrapper'],'sha256':hashlib.sha256(b).hexdigest(),'nodes':out,'one_based_entrypoints':entrypoints,'scope':'Native getter formulas; mode numbers preserved without invented enum names.'}
 (O/path.name).write_text(json.dumps(result,indent=2)+'\n')
 summary.append({'wrapper':old['wrapper'],'nodes':len(nodes),'entrypoints':len(entrypoints),'validated':True})
result={'wrappers':len(summary),'nodes':sum(x['nodes'] for x in summary),'entrypoints':sum(x['entrypoints'] for x in summary),'action_codes':dict(actions),'eligibility_modes':dict(filters),'evaluation_modes':dict(modes),'files':summary,'errors':bad}
(O/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
