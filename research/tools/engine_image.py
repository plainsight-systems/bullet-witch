"""Read-only address helpers for the hash-bound unpacked analysis image."""
from pathlib import Path
import struct,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
META=json.loads((ROOT/'experiments/steamstub/validation.json').read_text())['unpacked']
DATA=Path(META['path']).read_bytes()
assert hashlib.sha256(DATA).hexdigest()==META['sha256']
def offset(va):
 x=va-META['image_base'];s=next(s for s in META['sections'] if s['rva']<=x<s['rva']+s['file_bytes'])
 return s['file_offset']+x-s['rva']
def raw(va,n):return DATA[offset(va):offset(va)+n]
def u32(va):return struct.unpack('<I',raw(va,4))[0]
def string(va):
 p=offset(va);return DATA[p:DATA.index(b'\0',p)].decode('ascii',errors='replace')
def va_for_offset(p):
 s=next(s for s in META['sections'] if s['file_offset']<=p<s['file_offset']+s['file_bytes'])
 return META['image_base']+s['rva']+p-s['file_offset']
def imports():
 pe=struct.unpack_from('<I',DATA,0x3c)[0];rva=struct.unpack_from('<I',DATA,pe+24+104)[0];base=META['image_base'];out=[]
 a=base+rva
 while u32(a+12):
  dll=string(base+u32(a+12));names=base+(u32(a) or u32(a+16));iat=base+u32(a+16);i=0
  while (n:=u32(names+4*i)):
   out.append({'dll':dll,'name':f'ordinal:{n&65535}' if n&0x80000000 else string(base+n+2),'iat':hex(iat+4*i)})
   i+=1
  a+=20
 return out
