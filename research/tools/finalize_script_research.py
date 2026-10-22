#!/usr/bin/env python3
"""Assemble validated sources, per-script maps, AI node links and unresolved ledger."""
from pathlib import Path
from collections import Counter,defaultdict
import json,re,struct,hashlib,csv
from lua50_read import Reader
R=Path(__file__).resolve().parents[1];O=R/'evidence/script-corpus';G=Path('/Users/andyhunter/Games/BulletWitch-Steam')
D=R/'extracted/scripts-recovered';D.mkdir(exist_ok=True);N=O/'scripts';N.mkdir(exist_ok=True);A=O/'ai-nodes';A.mkdir(exist_ok=True)
inv=json.loads((O/'inventory.json').read_text());status={r['id']:r for r in json.loads((O/'no-locals/decompilation-status.json').read_text())};repairs={r['id']:r for r in json.loads((O/'local-repairs.json').read_text())};maps=json.loads((O/'script-native-map.json').read_text());api_pairs=json.loads((O/'native-api-pairs.json').read_text());byname=defaultdict(list)
origins=defaultdict(list)
for s in inv:origins[s['id']].append(s['origin'])
for r in api_pairs:
 if r['kind']=='verified-adapter-shape':byname[r['symbol']].append(r)
def save(n,obj):(O/n).write_text(json.dumps(obj,indent=2)+'\n')
def link(path,label):return f'[{label}]({Path(path).resolve().as_posix()})'
accepted=[];callback_links=[];resource_calls=[];unresolved_calls=[];native_names=set();defines=Counter();families=Counter();notes=[]
for s in inv:
 ident=s['id'];st=status[ident];repair=repairs.get(ident)
 if repair and repair.get('exact_normalized_match'):src=R/repair['source'];method='local-lifetime repair + exact normalized bytecode match'
 elif st['status']=='exact-normalized-roundtrip':src=R/st['decompiled_lua'];method='unluac no-locals hypothesis + exact normalized bytecode match'
 else:accepted.append({'id':ident,'status':'unresolved'});continue
 dst=D/(ident+'.lua');dst.write_bytes(src.read_bytes());family='embedded executable' if '!embedded@' in s['origin'] else ('AI' if '.amb.lub' in s['name'] else ('bonus mission' if '/concept/' in s['origin'] else ('test/sample' if s['name'].startswith(('test_','sample')) else 'root/stage')))
 families[family]+=1;m=json.loads((O/'maps'/(ident+'.json')).read_text());functions={f['name'] for f in m['functions'] if f['name']};native=set();resources=[];callbacks=[]
 for f in m['functions']:
  defines.update([f['name']] if f['name'] else [])
  for call in f['calls']:
   callee=call['callee'].get('global');args=call['arguments'];rec={'script':ident,'function':f['name'] or f['id'],'pc':call['pc'],'callee':callee,'arguments':args}
   if callee in byname:native.add(callee)
   elif callee not in functions:unresolved_calls.append(rec)
   if isinstance(args,list):
    for i,arg in enumerate(args):
     v=arg.get('constant')
     if not isinstance(v,str):continue
     if v in functions:callbacks.append({**rec,'argument':i,'callback':v});callback_links.append(callbacks[-1])
     if callee and any(w in callee for w in ('Load','Play','StartMission','SetRiffName')):resources.append({**rec,'argument':i,'literal':v});resource_calls.append(resources[-1])
 native_names.update(native)
 row={'id':ident,'origin':s['origin'],'family':family,'sha256':s['sha256'],'source':str(dst.relative_to(R)),'source_sha256':hashlib.sha256(dst.read_bytes()).hexdigest(),'status':'exact-normalized-roundtrip','method':method,'prototypes':s['prototypes'],'instructions':s['instructions'],'native_api_names':sorted(native),'script_functions':len(functions)};accepted.append(row)
 text=[f"# {s['name']}",'',f"Origin: `{s['origin']}`",'',f"Family: {family}. {s['prototypes']} prototypes; {s['instructions']} bytecode instructions.",'',f"Validation: **exact normalized bytecode round trip** ({method}). Variable names are reconstructed; comments and original formatting were not recovered.",'',link(dst,'Verified reconstructed Lua')+' · '+link(O/'listings'/(ident+'.txt'),'Original instruction listing')+' · '+link(O/'maps'/(ident+'.json'),'Function / call / branch map'),'',f"Input SHA-256: `{s['sha256']}`",'', '## Native API calls','',', '.join('`'+n+'`' for n in sorted(native)) or 'None identified.', '', '## Script functions','', '| Function | Instructions | Calls | Branches |','|---|---:|---:|---:|']
 for f in m['functions']:text.append(f"| {f['name'] or f['id']} | {f['instructions']} | {len(f['calls'])} | {len(f['branches'])} |")
 if resources:
  text+=['','## Resource literals','', 'These are literal arguments, not proof of a successful runtime load.','', '| Function | API | Literal |','|---|---|---|']
  text += [f"| {x['function']} | {x['callee']} | `{x['literal'].replace('|','&#124;')}` |" for x in resources]
 if callbacks:
  text+=['','## Callback names resolved in this script','']+[f"- `{x['function']}` → `{x['callback']}` through `{x['callee']}`." for x in callbacks]
 if len(origins[ident])>1:text+=['','## All locations of this identical chunk','']+['- `'+v+'`' for v in origins[ident]]
 (N/(ident+'.md')).write_text('\n'.join(text)+'\n');notes.append((row,N/(ident+'.md')))
save('accepted-sources.json',accepted);save('callback-links.json',callback_links);save('resource-call-literals.json',resource_calls);save('unresolved-script-calls.json',unresolved_calls)
# Cross-check every raw signature independently of CPR/DPK/AMB traversal.
known={s['sha256'] for s in inv};audit=[]
for f in json.loads((O/'raw-lua-signatures.json').read_text()):
 b=(G/f['path']).read_bytes()
 for off in f['offsets']:
  row={'file':f['path'],'offset':off}
  try:
   data=b[off:];expected=bytes.fromhex('1b4c756150010404040608090908')
   if data[:14]!=expected:raise ValueError('Not a supported Lua 5.0 chunk header: '+data[:22].hex())
   rr=Reader(data);rr.take(22);rr.function();h=hashlib.sha256(data[:rr.pos]).hexdigest();row.update(status='accounted-for' if h in known else 'unresolved-unextracted',bytes=rr.pos,sha256=h)
  except Exception as e:row.update(status='rejected-signature',reason=str(e))
  audit.append(row)
save('signature-audit.json',audit)
# Map AI 12-byte tree records to generated Lua functions; retain raw fields without guessed semantics.
ai_summary=[]
for w in json.loads((O/'ai-wrappers.json').read_text()):
 data=(R/'extracted/ai-wrappers'/w['name']).read_bytes();t=next(x for x in w['tags'] if x['tag']=='tree');start=t['offset']+8;assert data[start:start+4]==b'call';n=struct.unpack_from('<I',data,start+4)[0];calls=list(struct.unpack_from('<'+'I'*(n//4),data,start+8));nodebase=start+8+n;nodebytes=t['bytes']-8-n;assert nodebytes%12==0
 s=next(s for s in inv if s['name']==w['name']+'.lub');decoded=json.loads((O/'decoded'/(s['id']+'.json')).read_text())['root'];fns={int(f['global_name'].rsplit('_',1)[1]):f['global_name'] for f in decoded['children']};assert set(fns)==set(range(0,nodebytes,12))
 nodes=[{'offset':i,'lua_function':fns[i],'u32_fields':list(struct.unpack_from('<III',data,nodebase+i)),'call_slots':[j for j,v in enumerate(calls) if v==i+8]} for i in range(0,nodebytes,12)]
 entry={'wrapper':w['name'],'script_id':s['id'],'tree_node_bytes':12,'node_base_in_wrapper':nodebase,'call_offsets':calls,'call_offsets_point_to_third_field':all(v>=8 and (v-8)%12==0 and v<nodebytes for v in calls),'nodes':nodes,'caveat':'Exact structural link: Lua function numeric suffix equals tree-record byte offset. Raw u32 field and call-slot semantics not inferred.'}
 (A/(s['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n');ai_summary.append({'wrapper':w['name'],'nodes':len(nodes),'functions':len(fns),'call_offsets_valid':entry['call_offsets_point_to_third_field']})
save('ai-node-summary.json',ai_summary)
summary={'occurrences':len(inv),'unique_scripts':len({s['id'] for s in inv}),'accepted_occurrences':sum(r['status']=='exact-normalized-roundtrip' for r in accepted),'family_occurrences':dict(families),'prototypes':sum(s['prototypes'] for s in inv),'unique_prototypes':sum(s['prototypes'] for s in {s['id']:s for s in inv}.values()),'instructions':sum(s['instructions'] for s in inv),'unique_instructions':sum(s['instructions'] for s in {s['id']:s for s in inv}.values()),'native_apis_called':len(native_names),'unresolved_call_sites':len(unresolved_calls),'callback_links':len(callback_links),'resource_literal_arguments':len(resource_calls),'signature_audit':dict(Counter(x['status'] for x in audit)),'ai_tree_nodes':sum(x['nodes'] for x in ai_summary),'ai_wrappers':len(ai_summary),'ai_call_offsets_all_valid':all(x['call_offsets_valid'] for x in ai_summary)}
save('final-summary.json',summary)
index=['# Complete script corpus index','',f"{len(inv)} occurrences / {summary['unique_scripts']} unique chunks. All accepted sources were recompiled with Lua 5.0.3 and compared with the original bytecode. Every instruction, constant, prototype and stack/argument field matches after excluding debug metadata.",'','Use `scripts-recovered` as the canonical source directory. Earlier `scripts-decompiled`, `scripts-no-locals` and `scripts-finder` folders are retained experimental outputs; three no-locals files required repairs.','', '| Script | Family | Prototypes | Instructions | Verified source |','|---|---|---:|---:|---|']
for row,n in notes:index.append(f"| {link(n,row['id'])} | {row['family']} | {row['prototypes']} | {row['instructions']} | {link(R/row['source'],'Lua')} |")
(O/'INDEX.md').write_text('\n'.join(index)+'\n');print(json.dumps(summary,indent=2))
