#!/usr/bin/env python3
"""Snapshot PE imports and recognized callers from the saved Ghidra reference index."""
import json,csv,collections
from engine_image import ROOT,META,imports
idx=ROOT/'evidence/engine/pass-06/native-index/references.tsv'
rows=imports();by_iat={f"{int(r['iat'],16):08x}":r for r in rows}
for r in rows:r['recognized_references']=[]
with idx.open() as f:
 for ref in csv.DictReader(f,delimiter='\t'):
  if ref['to'] in by_iat:by_iat[ref['to']]['recognized_references'].append(ref)
result={'image_sha256':META['sha256'],'import_count':len(rows),'dlls':dict(collections.Counter(r['dll'] for r in rows)),'imports':rows,'reference_index':str(idx.relative_to(ROOT)),'scope':'PE imports and recognized references in a saved Ghidra checkpoint. Dynamic LoadLibrary/COM/Steam interface resolution is mapped separately; imports do not prove execution.'}
(ROOT/'evidence/engine/pass-06/platform-import-callers.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'imports':len(rows),'DLLs':len(result['dlls']),'recognized_references':sum(len(r['recognized_references']) for r in rows)}))
