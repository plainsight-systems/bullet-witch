#!/usr/bin/env python3
"""Map all native AI registry entries to concrete constructors and method slots."""
import json,re,collections
from engine_image import ROOT,META,u32,string
out=ROOT/'evidence/engine/pass-06/ai'
registry=json.loads((out/'action-factories.json').read_text())['records']
def source(a):
 ps=sorted((ROOT/'evidence/engine').glob(f'pass-*/functions/{a:08x}.c'))
 return ps[-1] if ps else None
families=[];methods=set()
for factory in sorted({int(r['factory'],16) for r in registry}):
 p=source(factory);c=p.read_text()
 matches=re.findall(r'\*puVar\d+ = &PTR_\w+_([0-9a-fA-F]{8});',c)
 ctor=factory
 if not matches:
  candidates=[]
  for s in set(re.findall(r'FUN_([0-9a-fA-F]{8})\(',c)):
   a=int(s,16)
   if a==factory:continue
   cp=source(a)
   if not cp:continue
   m=re.findall(r'\*param_1 = &PTR_\w+_([0-9a-fA-F]{8});',cp.read_text())
   if len(m)==1:candidates.append((a,m[0]))
  assert len(candidates)==1,(hex(factory),candidates)
  ctor,v=candidates[0]
 else:
  assert len(matches)==1,(hex(factory),matches)
  v=matches[0]
 vt=int(v,16);slots={hex(i):hex(u32(vt+i)) for i in range(0,0x24,4)}
 assert all(0x401000<=int(a,16)<0x6bf000 for a in slots.values())
 methods.update(int(a,16) for a in slots.values())
 rows=[r for r in registry if int(r['factory'],16)==factory]
 for r in rows:
  a=0x846fb8+r['code']*12
  assert u32(a)==factory and string(u32(a+4))==r['name'] and u32(a+8)==r['storage_bytes']
 fmt=[];mp=source(int(slots['0x20'],16))
 if mp:
  for s in re.findall(r'"(?:[^"\\]|\\.)*"',mp.read_text()):
   if '%' in s:fmt.append(s)
 families.append({'factory':hex(factory),'constructor':hex(ctor),'vtable':hex(vt),'first_nine_slots':slots,'actions':[r['name'] for r in rows],'debug_format_strings':fmt,'constructor_evidence':str(source(ctor).relative_to(ROOT))})
result={'image_sha256':META['sha256'],'registry_entries':len(registry),'unique_factories':len(families),'unique_method_targets':len(methods),'exported_method_targets':sum(source(a) is not None for a in methods),'factories':families,'scope':'Factory/constructor and first nine method pointers; numeric slot roles unless independently mapped. Debug format strings are clues, not full parameter schemas.'}
(out/'action-vtables.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'action-method-seeds.txt').write_text('\n'.join(f'{a:08x}' for a in sorted(methods))+'\n')
lines=['# Native AI action index','','All 109 registry entries, grouped into 48 allocation/constructor families. '+result['scope'],'','| Names | Factory / constructor | Vtable | Storage (bytes) | Debug parameter format |','|---|---|---|---|---|']
for f in families:
 names=', '.join('`'+n+'`' for n in f['actions']);p=source(int(f['factory'],16));cp=source(int(f['constructor'],16))
 sizes=sorted({r['storage_bytes'] for r in registry if r['name'] in f['actions']})
 lines.append(f"| {names} | [{f['factory']}](../../{p.relative_to(ROOT/'evidence/engine')}) / [{f['constructor']}](../../{cp.relative_to(ROOT/'evidence/engine')}) | {f['vtable']} | {', '.join(map(str,sizes))} | {'; '.join(f['debug_format_strings']).replace('|','&#124;')} |")
(out/'ACTION-INDEX.md').write_text('\n'.join(lines)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='factories'},indent=2))
