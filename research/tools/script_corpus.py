#!/usr/bin/env python3
"""Read-only whole-installation Lua/DPK discovery and per-chunk static mapping."""
from pathlib import Path
from local_inputs import game_directory
from collections import Counter
import hashlib,json,re,time,traceback,struct
from cpr_layout import inspect
from layout_probe import dpk
from lua50_read import parse,listing,Reader
R=Path(__file__).resolve().parents[1]; G=game_directory()
O=R/'evidence/script-corpus'; X=R/'extracted/scripts'
for p in (O,X,O/'decoded',O/'listings',O/'maps'):p.mkdir(parents=True,exist_ok=True)
rows=[];errors=[];containers=[];payload_ext=Counter();record_types=Counter();header_hits=[];ai_wrappers=[]
def save(n,obj):
 p=O/n;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(obj,indent=2)+'\n');t.replace(p)
def walk(f):
 yield f
 for c in f['children']:yield from walk(c)
def chunk(data,origin,name):
 h=hashlib.sha256(data).hexdigest(); ident=re.sub(r'[^a-zA-Z0-9_.-]','_',Path(name.replace('\\','/')).name)+'--'+h[:12]
 row={'id':ident,'origin':origin,'name':name,'bytes':len(data),'sha256':h,'extracted':'extracted/scripts/'+ident+'.lub'}
 (X/(ident+'.lub')).write_bytes(data)
 try:
  decoded=parse(data);fs=list(walk(decoded['root']));ops=Counter(i['op'] for f in fs for i in f['code'])
  (O/'decoded'/(ident+'.json')).write_text(json.dumps(decoded,indent=2)+'\n')
  (O/'listings'/(ident+'.txt')).write_text('\n'.join(listing(decoded['root']))+'\n')
  functions=[]
  for f in fs:
   regs={i:{'parameter':i} for i in range(f['parameter_count'])};calls=[];reads=[];writes=[];branches=[]
   leaders={i['target_pc'] for i in f['code'] if 'target_pc' in i}
   for i in f['code']:
    if i['pc'] in leaders:regs={}
    op=i['op'];a=i['A']
    if op=='GETGLOBAL':regs[a]={'global':i['constant']};reads.append(i['constant'])
    elif op=='LOADK':regs[a]={'constant':i['constant']}
    elif op=='MOVE':regs[a]=regs.get(i['B'],{'unknown_register':i['B']})
    elif op in ('CALL','TAILCALL'):
     calls.append({'pc':i['pc'],'callee':regs.get(a,{'unknown_register':a}),'arguments':[regs.get(j,{'unknown_register':j}) for j in range(a+1,a+i['B'])] if i['B'] else 'variable','argument_count':i['B']-1 if i['B'] else 'variable','result_count':i['C']-1 if i['C'] else 'variable'})
     # Function call may overwrite result registers; keep all later values conservative.
     regs={k:v for k,v in regs.items() if k<a}
    elif op=='SETGLOBAL':writes.append(i['constant'])
    elif op in ('ADD','SUB','MUL','DIV','POW','GETTABLE','CLOSURE'):regs.pop(a,None)
    if 'target_pc' in i:branches.append({'pc':i['pc'],'target':i['target_pc']})
    # Mapping above is linear within basic blocks, never evidence across control-flow merges.
    if op in ('JMP','EQ','LT','LE','TEST','FORLOOP','TFORLOOP','TFORPREP'):regs={}
   functions.append({'id':f['id'],'name':f.get('global_name'),'parameters':f['parameter_count'],'instructions':len(f['code']),'global_reads':sorted(set(reads)),'global_writes':sorted(set(writes)),'calls':calls,'branches':branches})
  mapping={'id':ident,'origin':origin,'functions':functions,'call_inference':'Straight-line register tracking, cleared at control-flow boundaries; unknown registers are unresolved, not guessed.'}
  (O/'maps'/(ident+'.json')).write_text(json.dumps(mapping,indent=2)+'\n')
  row.update(status='decoded',prototypes=len(fs),instructions=sum(ops.values()),opcodes=dict(ops),global_reads=sorted({v for f in functions for v in f['global_reads']}),global_writes=sorted({v for f in functions for v in f['global_writes']}),debug_lines=sum(len(f['lines']) for f in fs),debug_locals=sum(len(f['locals']) for f in fs))
 except Exception as e:row.update(status='unresolved',error=str(e));errors.append({'origin':origin,'stage':'Lua decode','error':str(e)})
 rows.append(row);save('inventory.json',rows)
def payload(data,origin,name,depth=0):
 if depth>8:errors.append({'origin':origin,'stage':'nested DPK','error':'Depth limit'});return
 payload_ext[Path(name).suffix.lower()]+=1
 if data.startswith(b'\x1bLua'):chunk(data,origin,name)
 elif data.startswith(b'ai\0\0'):
  try:
   if struct.unpack_from('<I',data,4)[0]!=len(data)-8:raise ValueError('AI wrapper size mismatch')
   pos=8;tags=[]
   while pos<len(data):
    tag=data[pos:pos+4];n=struct.unpack_from('<I',data,pos+4)[0];start=pos+8;end=start+n
    if end>len(data):raise ValueError('AI tag exceeds wrapper')
    record={'tag':tag.rstrip(b'\0').decode('ascii'),'offset':pos,'bytes':n}
    if tag==b'luac':
     size=struct.unpack_from('>I',data,start)[0];padding=data[start+4+size:end]
     if size>n-4 or len(padding)>3 or any(padding):raise ValueError('AI Lua size/padding mismatch')
     record.update(lua_bytes=size,lua_offset=start+4,padding_bytes=len(padding))
     chunk(data[start+4:start+4+size],origin+'!luac',name+'.lub')
    elif tag==b'labl':record['pairs_u32']=list(struct.iter_unpack('<II',data[start:end]))
    elif tag==b'str\0':record['strings']=[x.decode('utf-8',errors='backslashreplace') for x in data[start:end].split(b'\0') if x]
    tags.append(record);pos=end
   ai_wrappers.append({'origin':origin,'name':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'tags':tags})
   dest=R/'extracted/ai-wrappers';dest.mkdir(parents=True,exist_ok=True);(dest/Path(name).name).write_bytes(data)
  except Exception as e:errors.append({'origin':origin,'stage':'AI wrapper','error':str(e)})
 elif data.startswith(b'dpk\0'):
  try:
   info=dpk(data);containers.append({'origin':origin,'name':name,'version':info['version'],'members':len(info['entries'])})
   for e in info['entries']:
    if e['stored_bytes']!=e['declared_bytes']:
     errors.append({'origin':origin+'!'+e['name'],'stage':'DPK extraction','error':'Compressed member unresolved','stored':e['stored_bytes'],'declared':e['declared_bytes']});continue
    payload(data[e['offset']:e['offset']+e['stored_bytes']],origin+'!'+e['name'],e['name'],depth+1)
  except Exception as e:errors.append({'origin':origin,'stage':'DPK decode','error':str(e)})
 elif Path(name).suffix.lower() in ('.lua','.lub'):
  errors.append({'origin':origin,'stage':'script candidate','error':'Not a recognized binary Lua chunk','prefix':data[:32].hex()})
files=0;archives=0
for p in sorted(G.rglob('*')):
 if not p.is_file() or '.DepotDownloader' in p.parts:continue
 files+=1;rel=p.relative_to(G).as_posix();b=p.read_bytes()
 hits=[m.start() for m in re.finditer(b'\x1bLua',b)]
 if hits:header_hits.append({'path':rel,'offsets':hits,'versions':[b[i+4] if i+4<len(b) else None for i in hits]})
 if b.startswith(b'CPR1'):
  archives+=1
  try:
   for r in inspect(p)['records']:
    record_types[r['type']]+=1
    if r['payload_offset'] is not None and r['payload_bytes']:
     payload(b[r['payload_offset']:r['payload_offset']+r['payload_bytes']],rel+'!'+r['name'],r['name'])
  except Exception as e:errors.append({'origin':rel,'stage':'CPR decode','error':str(e)})
 else:payload(b,rel,p.name)
 if files%500==0:save('scan-progress.json',{'files':files,'archives':archives,'chunks':len(rows),'errors':len(errors),'last_path':rel})
# A final raw-signature pass discovers valid chunks embedded outside known containers.
known={r['sha256'] for r in rows}
for hit in header_hits:
 data=(G/hit['path']).read_bytes()
 for off in hit['offsets']:
  candidate=data[off:]
  if candidate[:14]!=bytes.fromhex('1b4c756150010404040608090908'):continue
  try:
   rr=Reader(candidate);rr.take(22);rr.function();candidate=candidate[:rr.pos];parse(candidate)
  except Exception:continue
  h=hashlib.sha256(candidate).hexdigest()
  if h not in known:
   chunk(candidate,hit['path']+f'!embedded@{off:#x}',f'embedded_{Path(hit["path"]).name}_{off:08x}.lub');known.add(h)
summary={'files_scanned':files,'cpr_archives_scanned':archives,'dpk_containers':len(containers),'ai_wrappers':len(ai_wrappers),'lua_occurrences':len(rows),'unique_lua_sha256':len({r['sha256'] for r in rows}),'decoded':sum(r['status']=='decoded' for r in rows),'prototypes':sum(r.get('prototypes',0) for r in rows),'instructions':sum(r.get('instructions',0) for r in rows),'errors':len(errors),'record_types':dict(record_types),'payload_extensions':dict(payload_ext.most_common()),'scope':'All non-downloader-metadata files; every CPR payload, recognized recursively nested DPK, ai/AMB luac tag and valid unaccounted raw Lua 5.0 signature. Unknown non-Lua formats not decompiled.'}
for name,obj in [('summary.json',summary),('containers.json',containers),('ai-wrappers.json',ai_wrappers),('errors.json',errors),('raw-lua-signatures.json',header_hits)]:save(name,obj)
print(json.dumps(summary,indent=2))
