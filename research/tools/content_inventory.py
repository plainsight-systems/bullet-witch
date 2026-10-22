#!/usr/bin/env python3
"""Catalog concept packages without modifying the game. Uses existing CPR/DPK readers."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import unicodedata
from cpr_layout import inspect
from layout_probe import dpk


def text_pool(data):
    if data[:8]!=b'STMANAGE':raise ValueError('Unexpected text signature')
    offset=struct.unpack_from('<I',data,28)[0]
    if not 0<=offset<len(data):raise ValueError('Invalid text pool offset')
    raw=[s.decode('utf-8') for s in data[offset:].split(b'\0') if s]
    normalized=[unicodedata.normalize('NFKC',s) for s in raw]
    # Costume strings can be shared across language entries, so English may
    # precede playtime. Mission English strings follow it in these 18 samples.
    # This is a scoped string selection, not a STMANAGE language-table parser.
    title=next((s for s in normalized if s.startswith('Special Costume ')),
               normalized[normalized.index('playtime')+1])
    return {'pool_offset':offset,'raw_strings':raw,'normalized_strings':normalized,
            'english_title':title}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();game=a.game.resolve();output=a.output.resolve()
    if output==game or game in output.parents:ap.error('Output must be outside game directory')
    output.mkdir(parents=True,exist_ok=True)
    packages=[]
    for folder in sorted((game/'data/concept').iterdir()):
        if not folder.is_dir():continue
        costume=folder.name.startswith('PW')
        package={'id':folder.name,'kind':'costume' if costume else 'mission',
          'path':folder.relative_to(game).as_posix(),'files':[], 'resources':[]}
        for f in sorted(x for x in folder.rglob('*') if x.is_file()):
            b=f.read_bytes();rel=f.relative_to(game).as_posix()
            package['files'].append({'path':rel,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
            if f.suffix!='.cpr':continue
            archive=inspect(f)
            for r in archive['records']:
                entry={k:r[k] for k in ['index','type','name','payload_offset','payload_bytes']}
                entry['archive']=rel
                if r['payload_offset'] is not None:
                    data=b[r['payload_offset']:r['payload_offset']+r['payload_bytes']]
                    entry['sha256']=hashlib.sha256(data).hexdigest()
                    if f.name in ['costxt.cpr','cmtxt.cpr']:
                        package['label']={'archive':rel,'member':r['name'],**text_pool(data)}
                    if f.name in ['cosspr.cpr','cmspr.cpr'] and r['type']=='DDS ':
                        assert data.startswith(b'\x89PNG\r\n\x1a\n')
                        package['preview']={'archive':rel,'member':r['name'],
                          'sha256':entry['sha256'],'dimensions':r.get('png_dimensions')}
                    if data.startswith(b'dpk\0'):
                        members=dpk(data)['entries']
                        package['dpk_members']=members
                        package['stage_references']=sorted({m.group(0) for e in members for m in re.finditer(r'st\d\d',e['name'])})
                        package['lua_members']=[e['name'] for e in members if e['name'].endswith('.lub')]
                package['resources'].append(entry)
        package['file_count']=len(package['files'])
        package['bytes']=sum(f['bytes'] for f in package['files'])
        package['resource_types']=dict(Counter(r['type'] for r in package['resources']))
        packages.append(package)
    result={'date':'2026-10-21','scope':'Immediate directories of data/concept',
      'package_count':len(packages),'counts':dict(Counter(p['kind'] for p in packages)),
      'files':sum(p['file_count'] for p in packages),'packages':packages}
    (output/'concept-packages.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    lines=['package\tkind\tEnglish title (NFKC)\tstage references\tfiles\tbytes']
    for p in packages:
        title=p['label']['english_title'].replace('\n',' / ')
        lines.append('\t'.join(map(str,[p['id'],p['kind'],title,','.join(p.get('stage_references',[])),p['file_count'],p['bytes']])))
    (output/'concept-packages.tsv').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))

if __name__=='__main__':main()
