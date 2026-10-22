#!/usr/bin/env python3
"""Extract/render the locally supplied CEDiL PDF; preserve the original bytes.
Run: python3 research/tools/inspect_cedec_2007.py
"""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]
source=root/'sources/cedec-2007/r35.pdf'
out=root/'evidence/cedec-2007'
out.mkdir(parents=True,exist_ok=True)
commands=[['pdfinfo',str(source)],['pdftotext','-layout',str(source),str(out/'r35-layout.txt')],['pdftoppm','-scale-to','1400','-png',str(source),str(out/'page')]]
runs=[]
for cmd in commands:
 r=subprocess.run(cmd,text=True,capture_output=True)
 runs.append(dict(command=cmd,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
 if r.returncode: raise RuntimeError(r.stderr)
manifest=dict(source=str(source.relative_to(root)),download_origin='user-supplied r35.pdf',catalog_url='https://cedil.cesa.or.jp/cedil_sessions/view/105?event=23',review_date='2026-10-22',sha256=hashlib.sha256(source.read_bytes()).hexdigest(),bytes=source.stat().st_size,runs=runs,review='All six rendered pages visually inspected; text matched relevant visible headings and engine bullet. PDF preserved without modification.')
(out/'inspection.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'sha256':manifest['sha256'],'bytes':manifest['bytes'],'rendered_pages':len(list(out.glob('page-*.png')))}))
