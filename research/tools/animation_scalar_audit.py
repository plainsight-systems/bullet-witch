#!/usr/bin/env python3
"""Compare the recovered 0x41df30 conversion against independent IEEE binary16 decoding."""
import struct,json
from engine_image import ROOT,META,raw

def native_bits(h):
 m=h&1023;e=(h>>10)&31
 if e==0:
  if m==0:e=-112
  else:
   e=1
   while not m&1024:m*=2;e-=1
   m&=1023
 return ((h&32768)<<16)|(m<<13)|((e+112)<<23)
finite=0;mismatches=[];special=[]
for h in range(65536):
 b=native_bits(h)
 if h&0x7c00==0x7c00:
  special.append(h);continue
 finite+=1
 independent=struct.unpack('<I',struct.pack('<f',struct.unpack('<e',struct.pack('<H',h))[0]))[0]
 if b!=independent:mismatches.append(h)
out={'image_sha256':META['sha256'],'helper':'0x41df30','finite_patterns':finite,'finite_mismatches':mismatches,'special_patterns_not_ieee_semantics':len(special),'examples':{hex(h):{'native_bits':hex(native_bits(h)),'native_float':struct.unpack('<f',struct.pack('<I',native_bits(h)))[0]} for h in [0,0x8000,1,0x3c00,0x7bff,0x7c00,0x7fff]},'slerp_near_parallel_threshold':struct.unpack('<f',raw(0x6c1bf0,4))[0],'limitation':'Algorithm transcription check against binary16, not execution of native x86 or validation of every animation channel.'}
p=ROOT/'evidence/engine/pass-06/animation-scalar-audit.json';p.write_text(json.dumps(out,indent=2)+'\n');assert not mismatches
print(json.dumps(out,indent=2))
