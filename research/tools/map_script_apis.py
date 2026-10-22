#!/usr/bin/env python3
"""Map every script global to native name/function pointer pairs and adapter bridges."""
import json,struct,re,hashlib
from pathlib import Path
from collections import Counter,defaultdict
R=Path(__file__).resolve().parents[1]; O=R/'evidence/script-corpus'; p=R/'experiments/steamstub/witch.original.exe.unpacked.exe';b=p.read_bytes()
u32=lambda o:struct.unpack_from('<I',b,o)[0];u16=lambda o:struct.unpack_from('<H',b,o)[0]
pe=u32(0x3c);opt=pe+24;base=u32(opt+28);sections=[]
for i in range(u16(pe+6)):
 o=opt+u16(pe+20)+40*i;vs,rva,size,raw=struct.unpack_from('<4I',b,o+8)
 sections.append({'name':b[o:o+8].rstrip(b'\0').decode(),'va':base+rva,'size':size,'raw':raw,'exec':bool(u32(o+36)&0x20000000)})
def offset(va):
 for s in sections:
  if s['va']<=va<s['va']+s['size']:return s['raw']+va-s['va']
def code(va):return any(s['exec'] and s['va']<=va<s['va']+s['size'] for s in sections)
def string(va):
 off=offset(va)
 if off is None:return None
 e=b.find(b'\0',off,min(off+160,len(b)))
 if e<0:return None
 v=b[off:e]
 if re.fullmatch(rb'[A-Za-z_][A-Za-z_0-9]*',v):return v.decode()
rows=[]
for s in sections:
 if s['exec']:continue
 for off in range(s['raw'],s['raw']+s['size']-7,4):
  strva,handler=struct.unpack_from('<II',b,off)
  if not code(handler):continue
  name=string(strva)
  if not name:continue
  h=offset(handler);stub=b[h:h+18];row={'symbol':name,'table_va':s['va']+off-s['raw'],'name_va':strva,'adapter_va':handler,'adapter_prefix':stub.hex(),'kind':'candidate-pointer-pair'}
  if stub[0]==0x68 and stub[5:10]==bytes.fromhex('ff742408e8') and stub[14:18]==bytes.fromhex('83c408c3'):
   callback=u32(h+1);bridge=handler+14+struct.unpack_from('<i',b,h+10)[0]
   row.update(callback_va=callback,bridge_va=bridge,kind='verified-adapter-shape',callback_executable=code(callback))
  rows.append(row)
byname=defaultdict(list)
for r in rows:byname[r['symbol']].append(r)
# Consecutive pairs expose table extents. Runtime registration call sites remain separate evidence.
tables=[]
for r in rows:
 if not tables or tables[-1]['end_va']!=r['table_va']:tables.append({'start_va':r['table_va'],'end_va':r['table_va']+8,'entries':[r]})
 else:tables[-1]['end_va']+=8;tables[-1]['entries'].append(r)
inv=json.loads((O/'inventory.json').read_text());uses=defaultdict(list);script_maps=[]
for s in inv:
 m=json.loads((O/'maps'/(s['id']+'.json')).read_text());defined=set(s['global_writes']);mapped=[]
 for f in m['functions']:
  for name in f['global_reads']:uses[name].append({'script':s['id'],'function':f.get('name') or f['id']})
 for name in s['global_reads']:
  mapped.append({'symbol':name,'classification':'script-defined' if name in defined else ('native-pair-found' if name in byname else 'unresolved-external-global'),'native_entries':byname.get(name,[])})
 script_maps.append({'id':s['id'],'origin':s['origin'],'globals':mapped})
api=[{'symbol':name,'uses':v,'native_entries':byname.get(name,[])} for name,v in sorted(uses.items())]
labels={}
for r in rows:
 if r['kind']!='verified-adapter-shape':continue
 labels.setdefault(r['adapter_va'],'LuaAdapter_'+r['symbol'])
 if 'callback_va' in r:labels.setdefault(r['callback_va'],'LuaNative_'+r['symbol'])
 labels.update({0x5607f0:'LuaBridge_Immediate',0x560780:'LuaBridge_Wait',0x560860:'Lua_RegisterTable',0x5608c0:'Lua_Pump',0x560de0:'Lua_LoadBufferAndCall',0x560f20:'Lua_PrepareGlobalCoroutine',0x43fc90:'Root_ScriptDriver',0x442120:'Script_LoadPackage',0x4409b0:'Script_MemberData',0x440a30:'Script_MemberLength',0x5dad40:'AI_RegisterScriptTable',0x43ecf0:'MissionAndRoot_Update'})
for name,value in [('native-api-pairs.json',rows),('native-api-tables.json',tables),('global-uses.json',api),('script-native-map.json',script_maps)]: (O/name).write_text(json.dumps(value,indent=2)+'\n')
(O/'native-labels.tsv').write_text(''.join(f'{va:08x}\t{name}\n' for va,name in sorted(labels.items())))
summary={'pair_candidates':len(rows),'verified_adapter_shapes':sum(r['kind']=='verified-adapter-shape' for r in rows),'contiguous_tables':len(tables),'globals_read':len(uses),'underscore_globals':sum(x.startswith('_') for x in uses),'underscore_globals_with_pairs':sum(x.startswith('_') and x in byname for x in uses),'unresolved_underscore_globals':[x for x in uses if x.startswith('_') and x not in byname],'bridges':dict(Counter(hex(r['bridge_va']) for r in rows if 'bridge_va' in r)),'native_labels':len(labels),'exe_sha256':hashlib.sha256(b).hexdigest(),'caveat':'Pointer-pair discovery and exact wrapper shape validate targets, not table registration on a live path or per-API behavior. Labels are research names.'}
(O/'native-api-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
