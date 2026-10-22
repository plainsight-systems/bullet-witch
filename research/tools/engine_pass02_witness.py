#!/usr/bin/env python3
"""Reproduce selected static table/registration witnesses for engine pass 02."""
from pathlib import Path
import hashlib,json,struct
root=Path(__file__).resolve().parents[1]
meta=json.loads((root/'experiments/steamstub/validation.json').read_text())['unpacked']
data=Path(meta['path']).read_bytes();digest=hashlib.sha256(data).hexdigest()
assert digest=='bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd'
def off(va):
 rva=va-meta['image_base'];s=next(s for s in meta['sections'] if s['rva']<=rva<s['rva']+s['file_bytes'])
 return s['file_offset']+rva-s['rva']
def u32(va):return struct.unpack_from('<I',data,off(va))[0]
def string(va):
 start=off(va);return data[start:data.index(b'\0',start)].decode('ascii')
def table(va,n):return {hex(i*4):hex(u32(va+4*i)) for i in range(n)}
registrations=[]
for site,name,callback in [(0x417f9e,0x6c30b0,0x418900),(0x418028,0x6c315c,0x419320)]:
 p=off(site);assert data[p]==0x68 and u32(site+1)==callback
 assert data[p+5]==0x68 and u32(site+6)==name and data[p+10]==0xe8
 target=site+15+struct.unpack_from('<i',data,p+11)[0];assert target==0x41b040
 registrations.append({'site':hex(site),'name':string(name),'callback':hex(callback),'registry':hex(target)})
assert u32(0x82a084+0x14)==0x43e990
assert u32(0x829ef8+8)==0x475880
assert u32(0x829f04+4)==0x415e50 and u32(0x829f04+8)==0x4163e0
out={'image_sha256':digest,'tables':{'orchestration':table(0x82a084,7),'worker':table(0x829ef8,3),'worker_lock':table(0x829f04,3),'application_hooks':table(0x6c2440,3)},'call_registrations':registrations,'area_names':[string(u32(0x956b40+i*4)) for i in range(4)],'model_suffixes':['.hit','.chit','.ehit','.vfx.default','.matanim.list.nocompress'],'scope':'Static file witnesses only. No execution or timing validation.'}
(root/'evidence/engine/pass-02/table-witnesses.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
