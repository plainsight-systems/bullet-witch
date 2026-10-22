#!/usr/bin/env python3
"""Run unluac over the complete corpus; retain failures and Lua 5.0 recompilation checks."""
from pathlib import Path
from collections import Counter
import json,subprocess,hashlib,time,struct,sys
from lua50_read import Reader,parse
R=Path(__file__).resolve().parents[1];O=R/'evidence/script-corpus';D=R/'extracted/scripts-decompiled';E=R/'experiments/decompilation'
mode=sys.argv[1] if len(sys.argv)>1 else 'nodebug'
inventory=json.loads((O/'inventory.json').read_text())
if len(sys.argv)>2 and sys.argv[2]=='unverified':
 pending={r['id'] for r in json.loads((O/'no-locals/decompilation-status.json').read_text()) if r['status']!='exact-normalized-roundtrip'}
 inventory=[r for r in inventory if r['id'] in pending]
if mode!='nodebug':
 D=R/('extracted/scripts-'+mode);O=O/mode;O.mkdir(parents=True,exist_ok=True);E=E/mode
JAVA=R/'vendor/jdk-25.0.4.1+1/Contents/Home/bin/java'; JAR=R/'vendor/unluac/unluac.jar';LUAC=R/'vendor/lua-5.0.3/bin/luac'
for p in (D,E/'logs',E/'recompiled',E/'comparisons'):p.mkdir(parents=True,exist_ok=True)
def save(n,v):
 p=O/n;t=p.with_suffix('.tmp');t.write_text(json.dumps(v,indent=2)+'\n');t.replace(p)
def nativeparse(data):
 h=data[:14]
 if h[:7]!=bytes.fromhex('1b4c7561500104') or h[7] not in (4,8) or h[8] not in (4,8) or h[9:]!=bytes.fromhex('0608090908'):raise ValueError('Unexpected compiler header '+h.hex())
 r=Reader(data,h[7],h[8]);r.take(22);f=r.function()
 if r.pos!=len(data):raise ValueError('Trailing compiler bytes')
 return f

def normalize(f):return {'params':f['parameter_count'],'upvalues':f['upvalue_count'],'vararg':f['vararg'],'stack':f['stack_size'],'constants':f['constants'],'instructions':[i['word'] for i in f['code']],'children':[normalize(c) for c in f['children']]}
rows=[];seen={}
for item in inventory:
 ident=item['id'];row={'id':ident,'origin':item['origin'],'source_sha256':item['sha256']}
 if ident in seen:row.update(seen[ident]);row['duplicate_provenance']=True;rows.append(row);save('decompilation-status.json',rows);continue
 t=time.monotonic();dst=D/(ident+'.lua')
 try:
  cmd=[str(JAVA),'-jar',str(JAR),'--nodebug',str(R/item['extracted'])]
  if mode!='nodebug':cmd=[str(JAVA),'-cp',str(JAR)+':'+str(R/'experiments/decompilation/harness'),'UnluacCorpus',mode,str(R/item['extracted'])]
  with dst.open('wb') as out,(E/'logs'/(ident+'.unluac.log')).open('wb') as log:
   proc=subprocess.run(cmd,stdout=out,stderr=log,timeout=90)
  row.update(decompiler_exit=proc.returncode,decompiled_lua=str(dst.relative_to(R)))
  if proc.returncode:raise RuntimeError('unluac exited '+str(proc.returncode))
  compiled=E/'recompiled'/(ident+'.lub')
  p=subprocess.run([str(LUAC),'-o',str(compiled),str(dst)],capture_output=True,timeout=30)
  (E/'logs'/(ident+'.luac.log')).write_bytes(p.stdout+p.stderr)
  row.update(compile_exit=p.returncode,source_bytes=dst.stat().st_size,source_output_sha256=hashlib.sha256(dst.read_bytes()).hexdigest())
  if p.returncode:row['status']='unresolved-recompile'
  else:
   original=parse((R/item['extracted']).read_bytes())['root'];rebuilt=nativeparse(compiled.read_bytes());a=normalize(original);b=normalize(rebuilt)
   row['normalized_bytecode_equal']=a==b
   row['status']='exact-normalized-roundtrip' if a==b else 'decompiled-syntax-checked'
   def flat(f):
    yield f
    for c in f['children']:yield from flat(c)
   af=list(flat(original));bf=list(flat(rebuilt))
   row['prototype_counts']=[len(af),len(bf)]
   row['instruction_counts']=[sum(len(f['code']) for f in af),sum(len(f['code']) for f in bf)]
   comparison={'equal':a==b,'original':a,'recompiled':b}
   if a!=b:(E/'comparisons'/(ident+'.json')).write_text(json.dumps(comparison,indent=2)+'\n')
 except Exception as e:row.update(status='unresolved',error=str(e))
 row['seconds']=round(time.monotonic()-t,3);rows.append(row);seen[ident]={k:v for k,v in row.items() if k not in ('origin',)};save('decompilation-status.json',rows)
 print(ident,row['status'],flush=True)
summary={'occurrences':len(rows),'unique_scripts':len(seen),'status_occurrences':dict(Counter(r['status'] for r in rows)),'status_unique':dict(Counter(r['status'] for r in seen.values())),'validation':'unmodified Lua 5.0.3 compiler on arm64; its native 8-byte size_t and Instruction serialization decoded separately. Normalized comparison excludes debug/source/line data but includes all instructions, constants, stack/parameter/upvalue/vararg metadata and nested prototypes. Syntax checks alone do not prove equivalence.'}
save('decompilation-summary.json',summary);print(json.dumps(summary,indent=2))
