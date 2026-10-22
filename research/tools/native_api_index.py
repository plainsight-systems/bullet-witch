#!/usr/bin/env python3
"""Index verified native adapters, callsite argument counts, and Ghidra exports."""
import json,csv
from pathlib import Path
from collections import Counter,defaultdict
R=Path(__file__).resolve().parents[1];O=R/'evidence/script-corpus';N=R/'evidence/native-decompilation'
rows=json.loads((O/'native-api-pairs.json').read_text());verified=[r for r in rows if r['kind']=='verified-adapter-shape'];tables=[t for t in json.loads((O/'native-api-tables.json').read_text()) if any(r['kind']=='verified-adapter-shape' for r in t['entries'])]
stat={r['address']:r for r in csv.DictReader((N/'status.tsv').open(),delimiter='\t')};uses=defaultdict(list);edges=defaultdict(list)
for p in (O/'maps').glob('*.json'):
 d=json.loads(p.read_text())
 for f in d['functions']:
  for c in f['calls']:
   name=c['callee'].get('global')
   if name:uses[name].append({'script':d['id'],'function':f['name'] or f['id'],**c})
for e in csv.DictReader((N/'calls.tsv').open(),delimiter='\t'):edges[e['caller']].append(e)
api=[];seen=set()
for pair in verified:
 name=pair['symbol'];key=(name,pair['adapter_va'],pair['callback_va'])
 if key in seen:continue
 seen.add(key);va=f"{pair['callback_va']:08x}";entry={**pair,'decompiler_status':stat.get(va),'callsite_count_unique_chunks':len(uses[name]),'observed_argument_counts':dict(Counter(str(c.get('argument_count','unrecorded')) for c in uses[name])),'observed_result_counts':dict(Counter(str(c.get('result_count','unrecorded')) for c in uses[name])),'callers':uses[name],'native_calls':edges[va],'c_source':f'evidence/native-decompilation/functions/{va}.c','bridge_mode':'wait/yield' if pair['bridge_va']==0x560780 else 'immediate'};api.append(entry)
(O/'native-api-index.json').write_text(json.dumps(api,indent=2)+'\n')
text=['# Native script API index','','290 registration entries in three statically identified tables; 273 distinct names and 272 unique adapter/callback addresses. 214 names are called by the extracted scripts. All registered callbacks were included in Ghidra export, including unused APIs.','','Argument/result counts below describe static Lua callsites, not recovered native types. C is automatic pseudocode with inferred types; it has not been executed or rebuilt.','','| API | Mode | Adapter | Callback C | Calls in unique chunks | Argument counts |','|---|---|---|---|---:|---|']
for a in sorted(api,key=lambda x:(x['symbol'],x['table_va'])):
 source=R/a['c_source'];text.append(f"| `{a['symbol']}` | {a['bridge_mode']} | `0x{a['adapter_va']:08x}` | [0x{a['callback_va']:08x}]({source}) | {a['callsite_count_unique_chunks']} | {', '.join(a['observed_argument_counts']) or 'unused'} |")
text+=['','## Duplicate binding in the AI table','','`getTargetKnowledgeLevel` occurs twice consecutively: table 0x96b520 → adapter 0x5d0ff0 / callback 0x5d44c0, then 0x96b528 → adapter 0x5d13f0 / callback 0x5d8130. The first shares the previous-enemy-knowledge handler. The registration loop processes entries in order and sets Lua globals, so the later binding supersedes the earlier one when this table is registered. Both mappings are retained.','','## Boundaries','','The standalone candidate `z` in a different pointer pair is a script-defined variable and is not counted as a native script API. Only the 18-byte verified adapter pattern is accepted here. Native virtual/function-pointer calls remain separately listed in `native-decompilation/calls.tsv`.']
(O/'NATIVE-API.md').write_text('\n'.join(text)+'\n')
summary={'registration_tables':[{'va':hex(t['start_va']),'entries':len(t['entries'])} for t in tables],'registration_entries':len(verified),'distinct_names':len({r['symbol'] for r in verified}),'distinct_adapters':len({r['adapter_va'] for r in verified}),'distinct_callbacks':len({r['callback_va'] for r in verified}),'called_native_names':len({r['symbol'] for r in verified if uses[r['symbol']]}),'all_registered_callbacks_decompiled':all(stat.get(f"{r['callback_va']:08x}",{}).get('status')=='ok' for r in verified),'native_export_functions':len(stat),'native_export_failures':sum(r['status']!='ok' for r in stat.values()),'indirect_call_sites':sum(e['kind']=='indirect-unresolved' for es in edges.values() for e in es),'native_seed_labels':sum(r['seed']=='true' for r in stat.values())}
(O/'native-final-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
