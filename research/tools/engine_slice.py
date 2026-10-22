#!/usr/bin/env python3
"""Export one reproducible native slice using the established research project."""
from pathlib import Path
import argparse,json,os,subprocess
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('name');p.add_argument('seeds',nargs='+');a=p.parse_args()
assert a.name.startswith('pass-') and '/' not in a.name
out=r/'evidence/engine'/a.name;out.mkdir(exist_ok=False)
(out/'seeds.txt').write_text('\n'.join(f'{int(s,16):08x}' for s in a.seeds)+'\n')
prior=json.loads((r/'evidence/engine/pass-05h/command.json').read_text())
# Historical commands retain the original checkout path; rebase executable,
# project, scripts and output paths before making a new export.
old_r=next(v.split('/evidence/engine/pass-05h')[0] for v in prior['command'] if '/evidence/engine/pass-05h' in v)
def rebase(v):
 return v.replace(old_r,str(r)).replace(str(r/'evidence/engine/pass-05h'),str(out))
cmd=[rebase(v) for v in prior['command']]
overrides={k:rebase(v) for k,v in prior['environment_overrides'].items()}
(out/'command.json').write_text(json.dumps({'command':cmd,'environment_overrides':overrides},indent=2)+'\n')
env=os.environ.copy();env.update(overrides)
with (out/'console.log').open('w') as log:result=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
print(json.dumps({'slice':a.name,'returncode':result.returncode,'status_file':str(out/'status.tsv')}))
raise SystemExit(result.returncode)
