#!/usr/bin/env python3
"""Supplementary read-only layout probes. Run after cpr_layout.py."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from cpr_layout import inspect


def cstr(b, pos, end=None):
    if end is None:end=len(b)
    if not 0<=pos<end:raise ValueError('String offset out of bounds')
    stop=b.find(b'\0',pos,end)
    if stop<0:raise ValueError('Unterminated string')
    return b[pos:stop].decode('utf-8',errors='replace')


def dpk(b):
    if b[:4]!=b'dpk\0':raise ValueError('Not DPK')
    info,version,base,count=struct.unpack_from('<4I',b,4)
    if info+count*32>len(b):raise ValueError('DPK table out of bounds')
    entries=[]
    for i in range(count):
        compressed,size,offset,name=struct.unpack_from('<4I',b,info+i*32+16)
        if offset+compressed>len(b):raise ValueError('DPK payload out of bounds')
        payload=b[offset:offset+compressed]
        e={'name':cstr(b,name),'offset':offset,'stored_bytes':compressed,
           'declared_bytes':size,'prefix_hex':payload[:24].hex(),
           'sha256':hashlib.sha256(payload).hexdigest()}
        if payload.startswith(b'\x1bLua'):
            e['lua_version_byte']=payload[4]
            e['selected_strings']=[m.group().decode() for m in re.finditer(rb'[ -~]{5,}',payload)][:80]
        entries.append(e)
    return {'version':version,'data_offset':base,'entries':entries}


def rat(path):
    b=path.read_bytes()
    if b[:4]!=b'RAT ':raise ValueError('Not RAT')
    declared=struct.unpack_from('<I',b,4)[0]
    if declared!=len(b):raise ValueError('RAT declared size mismatch')
    sections=[struct.unpack_from('<II',b,i) for i in range(0x10,0x40,8)]
    for off,size in sections:
        if off+size>len(b):raise ValueError('RAT section out of bounds')
    namebase,namesize=sections[5]
    def name(off):return cstr(b,namebase+off,namebase+namesize)
    base,size=sections[4]
    if size%8:raise ValueError('RAT file table alignment')
    refs=[]
    for i in range(base,base+size,8):
        di,fi=struct.unpack_from('<II',b,i)
        directory,filename=name(di),name(fi)
        rel=('/'.join([directory,filename])).strip('/').replace('\\','/')
        target=path.parent/rel
        refs.append({'index':len(refs),'directory':directory,'filename':filename,
                     'relative_path':rel,'exists':target.is_file()})
    base,size=sections[2]
    if size%20:raise ValueError('RAT resource table alignment')
    resources=[]
    for i in range(base,base+size,20):
        a,parent,c,file_index,extra=struct.unpack_from('<5I',b,i)
        if file_index>=len(refs):raise ValueError('RAT file index out of bounds')
        resources.append({'name':name(a),'tree_offset':parent,'unknown_08':c,
                          'file_index':file_index,'unknown_10':extra})
    return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),
            'sections':sections,'files':refs,'resources':resources}


def shader_candidates(b):
    results=[]
    # Require an SM2/SM3 version token immediately followed by a CTAB comment.
    # Walking token lengths checks boundaries; it is not a GPU semantic validator.
    for m in re.finditer(rb'\x00[\x02\x03][\xfe\xff]\xff\xfe\xff..CTAB',b,re.S):
        start=m.start();pos=start+4;instructions=0;comments=0
        while pos+4<=len(b) and pos-start<1024*1024:
            token=struct.unpack_from('<I',b,pos)[0]
            op=token&0xffff
            if token==0xffff:
                payload=b[start:pos+4]
                results.append({'file_offset':start,'bytes':len(payload),
                    'profile':('vs' if b[start+2]==0xfe else 'ps')+'_'+str(b[start+1])+'_0',
                    'instructions':instructions,'comments':comments,
                    'sha256':hashlib.sha256(payload).hexdigest()})
                break
            if token&0x80000000:break
            if op==0xfffe:
                length=(token>>16)&0x7fff;comments+=1
            elif op<=96:
                length=(token>>24)&15;instructions+=1
            else:break
            pos+=4*(1+length)
    return results


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();game=a.game.resolve();out=a.output.resolve()
    if out==game or game in out.parents:ap.error('Output must be outside game directory')
    out.mkdir(parents=True,exist_ok=True)
    def save(name,value):(out/name).write_text(json.dumps(value,indent=2)+'\n')
    rats={};rat_errors=[]
    for f in sorted(game.rglob('*.rat')):
        try:rats[f.relative_to(game).as_posix()]=rat(f)
        except (ValueError,struct.error) as e:rat_errors.append({'path':str(f),'error':str(e)})
    save('rat-layout.json',rats)
    dpk_records=[];dpk_errors=[]
    for f in sorted((game/'data/data/stable/script').glob('*.cpr')):
        b=f.read_bytes()
        for r in inspect(f)['records']:
            if r['payload_offset'] is None:continue
            data=b[r['payload_offset']:r['payload_offset']+r['payload_bytes']]
            if not data.startswith(b'dpk\0'):continue
            try:dpk_records.append({'archive':f.relative_to(game).as_posix(),'member':r['name'],**dpk(data)})
            except (ValueError,struct.error) as e:dpk_errors.append({'archive':str(f),'error':str(e)})
    save('script-dpk-layout.json',dpk_records)
    exe=(game/'witch.exe').read_bytes();shaders=shader_candidates(exe)
    save('embedded-shader-candidates.json',shaders)
    sr=inspect(game/'data/shader/shaders.cpr')['records']
    ids=sorted(set(r['name'].split('_')[0] for r in sr if r['type'] in ['DXPS','DXVS']))
    dirs=[]
    for base in ['data','data/data/stable','data/data/stable/graphics','data/data/stable/graphics/bg','data/concept']:
        for f in sorted((game/base).iterdir()):
            fs=[x for x in f.rglob('*') if x.is_file()] if f.is_dir() else [f]
            dirs.append({'path':f.relative_to(game).as_posix(),'files':len(fs),'bytes':sum(x.stat().st_size for x in fs)})
    save('directory-map.json',dirs)
    media=[]
    for f in sorted((game/'data/movie').glob('*'))+sorted((game/'data/sound').glob('*.xwv')):
        r=subprocess.run(['ffprobe','-v','error','-show_entries','format=format_name,duration:stream=index,codec_name,codec_type,width,height,sample_rate,channels','-of','json',str(f)],capture_output=True,text=True)
        media.append({'path':f.relative_to(game).as_posix(),'result':json.loads(r.stdout) if r.stdout else {},'error':r.stderr.strip(),'exit_code':r.returncode})
    save('media-probes.json',media)
    manifests=[{'depot':f.name.split('_')[0],'manifest':f.name.split('_')[1].split('.')[0]} for f in sorted((game/'.DepotDownloader').glob('*.manifest'))]
    save('depot-manifests.json',manifests)
    referenced={(game/Path(p).parent/f['relative_path']).relative_to(game).as_posix()
                for p,r in rats.items() for f in r['files']}
    actual={f.relative_to(game).as_posix() for f in game.rglob('*.cpr')}
    summary={'rat_files_parsed':len(rats),'rat_errors':rat_errors,
      'rat_archive_references':sum(len(r['files']) for r in rats.values()),
      'rat_unique_archive_references':len(referenced),
      'rat_references_match_all_cprs':referenced==actual,
      'rat_missing_references':[{'rat':p,**f} for p,r in rats.items() for f in r['files'] if not f['exists']],
      'rat_resource_count':sum(len(r['resources']) for r in rats.values()),
      'script_dpk_count':len(dpk_records),'script_dpk_errors':dpk_errors,
      'script_member_extensions':dict(Counter(Path(e['name']).suffix for r in dpk_records for e in r['entries'])),
      'compressed_script_members':sum(e['stored_bytes']!=e['declared_bytes'] for r in dpk_records for e in r['entries']),
      'lua_header_versions':dict(Counter(str(e['lua_version_byte']) for r in dpk_records for e in r['entries'] if 'lua_version_byte' in e)),
      'shader_cpr_records':len(sr),'shader_cpr_nonempty_payloads':sum(r['payload_bytes']>0 for r in sr),
      'shader_variant_ids':len(ids),'shader_ids_present_in_exe':sum(i.encode()+b'\0' in exe for i in ids),
      'shader_candidates':len(shaders),'shader_profiles':dict(Counter(r['profile'] for r in shaders)),
      'media_probe_errors':sum(r['exit_code']!=0 for r in media)}
    save('supplementary-summary.json',summary);print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
