#!/usr/bin/env python3
"""Read-only PC structure comparison guided by Rich Whitehouse's Noesis CPR reader.
Independent bounded probe, not a model importer. Outputs metadata only.
"""
import argparse, collections, hashlib, json, struct
from pathlib import Path
from cpr_layout import inspect


def compare(path):
    data = path.read_bytes(); arc = inspect(path)
    entries = {e['name']: e for e in arc['records']}
    def payload(name):
        e = entries[name]; return data[e['payload_offset']:e['payload_offset']+e['payload_bytes']]
    def u32(b, o): return struct.unpack_from('<I', b, o)[0]
    hdr = payload('header.bin')
    assert hdr[:4] == b'2XFV'
    pos = 0x60 + u32(hdr, 0x5c) - 12
    chunks = []; tags = collections.Counter(); declarations = []; buffers = []; draws = []; maps = []; refs = []
    def scan(start, end, scope):
        p = start
        while p < end:
            assert p+8 <= end, (scope,p,end)
            tag = hdr[p:p+4][::-1].decode('ascii'); size = u32(hdr,p+4); p += 8
            assert p+size <= end, (scope,tag,p,size,end)
            tags[tag] += 1
            if tag == 'VDCL':
                # PC embeds 8-byte D3DVERTEXELEMENT9 records after a 4-byte prefix.
                # The console reader's 12-byte records do not apply here.
                elements=[]; q=p+4; terminated=False
                while q+8<=p+size:
                    element=list(struct.unpack_from('<HH4B',hdr,q));q+=8
                    if element == [255,0,17,0,0,0]:
                        terminated=True;break
                    assert element[2]<17 and element[3]<=6 and element[4]<=13
                    elements.append(dict(zip(('stream','offset','type','method','usage','usage_index'),element)))
                assert terminated
                declarations.append({'offset':p-8,'size':size,'prefix':u32(hdr,p),'elements':elements,
                    'trailing_bytes':p+size-q,'trailing_nonzero':any(hdr[q:p+size])})
            elif tag in ('VTX ', 'VTXS'):
                assert size == 16
                buffers.append({'tag':tag,'fields':list(struct.unpack_from('<4i',hdr,p))})
            elif tag == 'DIP ':
                assert size == 24
                draws.append(list(struct.unpack_from('<6i',hdr,p)))
            elif tag == 'CM1R':
                n=u32(hdr,p+4); assert 8+n*16 == size
                joints=[]
                for q in range(p+8,p+size,16):
                    assert hdr[q:q+4][::-1] == b'JINT'
                    joints.append(list(struct.unpack_from('<3i',hdr,q+4)))
                maps.append(joints)
            elif tag == 'TR  ':
                n=u32(hdr,p+4); assert 8+n*16 == size
                refs.extend({'type':hdr[q:q+4][::-1].decode('ascii'),'type_bytes':hdr[q:q+8].hex(),'indices':list(struct.unpack_from('<2i',hdr,q+8))} for q in range(p+8,p+size,16))
            # Noesis treats these lengths as containing nested chunks and skips their small prefixes.
            elif tag in ('DGRP','DVOL'):
                scan(p+4,p+size,tag)
            elif tag in ('kns2','knsC'):
                scan(p,p+size,tag)
            elif tag == 'MCTL':
                scan(p+8,p+size,tag)
            p += size
        assert p == end
    while pos+12 <= len(hdr) and hdr[pos+4:pos+8] == b'HKNC':
        idx=u32(hdr,pos); size=u32(hdr,pos+8); start=pos+12
        assert start+size <= len(hdr)
        chunks.append({'index':idx,'offset':pos,'size':size})
        scan(start,start+size,'CNKH');pos=start+size
    sk=payload('motion._bl'); n=u32(sk,4)
    assert sk[:4]==b'_BLB' and len(sk)==8+n*104
    parents=[struct.unpack_from('<i',sk,8+i*104+4)[0] for i in range(n)]
    assert all(-1<=p<n for p in parents)
    tx=payload('texlist.bin'); nt=u32(tx,4)
    assert tx[:4]==b'TLST' and len(tx)==8+nt*36
    names=[tx[12+i*36:44+i*36].split(b'\0')[0].decode('ascii') for i in range(nt)]
    assert all(0 <= ref['indices'][0] < nt for ref in refs)
    assert all(0 <= joint[1] < n for bone_map in maps for joint in bone_map)
    for binding in buffers:
        fields=binding['fields'];prefix='shapevertex' if binding['tag']=='VTXS' else 'vertex'
        entry=entries[f'{prefix}_{fields[1]:03d}.bin']
        assert fields[3]>0 and entry['payload_bytes']%fields[3]==0
        binding['vertex_count']=entry['payload_bytes']//fields[3]
    return {'source':str(path),'sha256':arc['sha256'],'header_bytes':len(hdr),'parsed_header_end':pos,
      'header_unparsed_bytes':len(hdr)-pos,'chunks':chunks,'chunk_tags':dict(tags),'vertex_declarations':declarations,
      'vertex_bindings':buffers,'draw_parameters':draws,'bone_maps':maps,'texture_references':refs,
      'skeleton':{'bones':n,'record_stride':104,'parents':parents},'texture_names':names,
      'archive_buffer_records':[{k:e[k] for k in ('type','name','payload_bytes')} for e in arc['records'] if e['type'] in ('DXVB','DXIB')],
      'scope':'Structural metadata comparison only; vertex interpretation, rendered appearance and animation decoding are not verified.'}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('input',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    game=Path('/Users/andyhunter/Games/BulletWitch-Steam').resolve()
    if a.output.resolve()==game or game in a.output.resolve().parents:ap.error('Output must stay outside game directory')
    result=compare(a.input);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('header_bytes','parsed_header_end','header_unparsed_bytes','chunk_tags')},indent=2))
