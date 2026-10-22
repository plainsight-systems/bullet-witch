#!/usr/bin/env python3
"""Extract and statically decode the three startup-related Lua packages."""
import hashlib,json
from pathlib import Path
from local_inputs import game_directory
from cpr_layout import inspect
from layout_probe import dpk
from lua50_read import parse,listing
research=Path(__file__).resolve().parents[1]
game=game_directory()
out=research/'evidence/startup';extracted=research/'extracted/startup'
out.mkdir(parents=True,exist_ok=True);extracted.mkdir(parents=True,exist_ok=True)
records=[]
for stem in ['root','root_riff','default_callback']:
    path=game/f'data/data/stable/script/{stem}.cpr';blob=path.read_bytes()
    for r in inspect(path)['records']:
        if r['payload_offset'] is None:continue
        data=blob[r['payload_offset']:r['payload_offset']+r['payload_bytes']]
        for e in dpk(data)['entries']:
            assert e['stored_bytes']==e['declared_bytes']
            chunk=data[e['offset']:e['offset']+e['stored_bytes']]
            name=e['name'];assert Path(name).name==name
            result=parse(chunk)
            (extracted/name).write_bytes(chunk)
            (out/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n')
            (out/(name+'.txt')).write_text('\n'.join(listing(result['root']))+'\n')
            records.append({'archive':path.relative_to(game).as_posix(),'archive_sha256':hashlib.sha256(blob).hexdigest(),
                            'member':name,'sha256':hashlib.sha256(chunk).hexdigest(),'bytes':len(chunk),'parsed_bytes':result['bytes_consumed']})
(out/'extraction.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
