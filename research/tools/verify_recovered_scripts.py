#!/usr/bin/env python3
"""Independently recompile every canonical recovered Lua file; never execute it."""
from pathlib import Path
from collections import Counter
import json,hashlib,subprocess,time
from lua50_read import parse,Reader
R=Path(__file__).resolve().parents[1];O=R/'evidence/script-corpus';E=R/'experiments/decompilation/final-verification';E.mkdir(exist_ok=True)
manifest=json.loads((O/'accepted-sources.json').read_text());inventory={r['id']:r for r in json.loads((O/'inventory.json').read_text())};rows=[]
def normalize(f):return [f['parameter_count'],f['upvalue_count'],f['vararg'],f['stack_size'],f['constants'],[i['word'] for i in f['code']],[normalize(c) for c in f['children']]]
def digest(obj):return hashlib.sha256(json.dumps(obj,separators=(',',':'),ensure_ascii=True).encode()).hexdigest()
for s in {s['id']:s for s in manifest}.values():
 ident=s['id'];row={'id':ident};p=R/s['source'];compiled=E/(ident+'.lub')
 try:
  olddata=(R/inventory[ident]['extracted']).read_bytes();assert hashlib.sha256(olddata).hexdigest()==s['sha256'];assert hashlib.sha256(p.read_bytes()).hexdigest()==s['source_sha256']
  proc=subprocess.run([str(R/'vendor/lua-5.0.3/bin/luac'),'-o',str(compiled),str(p)],capture_output=True,timeout=30)
  (E/(ident+'.log')).write_bytes(proc.stdout+proc.stderr)
  if proc.returncode:raise ValueError('Compiler exit '+str(proc.returncode))
  data=compiled.read_bytes();assert data[:14]==bytes.fromhex('1b4c756150010408080608090908');rr=Reader(data,8,8);rr.take(22);rebuilt=rr.function();assert rr.pos==len(data)
  old=normalize(parse(olddata)['root']);new=normalize(rebuilt);row.update(original_normalized_sha256=digest(old),recompiled_normalized_sha256=digest(new),status='pass' if old==new else 'unresolved-bytecode-difference')
 except Exception as e:row.update(status='unresolved',error=str(e))
 rows.append(row);tmp=O/'final-verification.tmp';tmp.write_text(json.dumps(rows,indent=2)+'\n');tmp.replace(O/'final-verification.json')
# Check preserved source hashes against the earlier whole-archive inventory.
archive_errors=[];archives=json.loads((R/'evidence/layout/cpr-inventory.json').read_text());game=Path('/Users/andyhunter/Games/BulletWitch-Steam')
for a in archives:
 if hashlib.sha256((game/a['path']).read_bytes()).hexdigest()!=a['sha256']:archive_errors.append(a['path'])
exe_hash=hashlib.sha256((game/'witch.exe').read_bytes()).hexdigest();unpacked_hash=hashlib.sha256((R/'experiments/steamstub/witch.original.exe.unpacked.exe').read_bytes()).hexdigest()
summary={'unique_sources_checked':len(rows),'status':dict(Counter(r['status'] for r in rows)),'original_cpr_hashes_checked':len(archives),'changed_cpr_archives':archive_errors,'original_exe_sha256':exe_hash,'original_exe_unchanged':exe_hash=='f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc','unpacked_exe_sha256':unpacked_hash,'unpacked_exe_unchanged':unpacked_hash=='bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd','game_scripts_executed':False}
(O/'final-verification-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
