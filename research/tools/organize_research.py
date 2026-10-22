#!/usr/bin/env python3
"""One-time documentation relocation; preserve binaries and repair project Markdown links."""
from pathlib import Path
import re,os,json
P=Path(__file__).resolve().parents[2];R=P/'research'
moves={p:R/'reports'/p.name for p in R.glob('2026-*.md') if 'worklog' not in p.name}
moves.update({p:R/'logs'/p.name for p in R.glob('*worklog.md')})
moves[R/'ENGINE-ARCHITECTURE.md']=R/'engine/ARCHITECTURE.md'
moves[R/'open-questions.md']=R/'engine/OPEN-QUESTIONS.md'
# References written during this turn before the subsystem notes were created.
logical={R/'engine/execution-and-scripts.md':R/'engine/subsystems/execution-and-scripts.md',R/'engine/mission-and-stage-loading.md':R/'engine/subsystems/mission-and-stage-loading.md'}
remap={**moves,**logical}
docs=[p for p in P.rglob('*.md') if 'vendor' not in p.relative_to(P).parts and '.git' not in p.parts]
updated=[]
for old in docs:
 dest=moves.get(old,old);body=old.read_text();count=[0]
 def sub(m):
  raw=m.group(2);token=raw.strip('<>')
  if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',token) or token.startswith('#'):return m.group(0)
  path,sep,anchor=token.partition('#');target=(old.parent/path).resolve() if not Path(path).is_absolute() else Path(path)
  newtarget=remap.get(target,target)
  newpath=str(newtarget) if Path(path).is_absolute() else os.path.relpath(newtarget,dest.parent)
  if path.endswith('/') and not newpath.endswith('/'):newpath+='/'
  newtoken=newpath+(sep+anchor if sep else '')
  if raw.startswith('<'):newtoken='<'+newtoken+'>'
  if raw!=newtoken:count[0]+=1
  return m.group(1)+newtoken+')'
 body=re.sub(r'(!?\[[^\]\n]*\]\()([^\n)]+)\)',sub,body)
 if dest!=old or count[0]:
  dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(body)
  if dest!=old:old.unlink()
  updated.append({'from':str(old.relative_to(P)),'to':str(dest.relative_to(P)),'links_updated':count[0]})
(R/'evidence/engine/research-relocation.json').write_text(json.dumps(updated,indent=2)+'\n')
print(json.dumps(updated,indent=2))
