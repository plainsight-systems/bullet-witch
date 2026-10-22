#!/usr/bin/env python3
"""Reproduce address/table/string evidence for the manually traced startup map.

This uses the known unpacked build. Names for inferred roles are analyst labels;
the tool verifies bytes and table connections, not runtime behavior.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

r=Path(__file__).resolve().parents[1]
out=r/'evidence/native-startup'
out.mkdir(parents=True,exist_ok=True)
v=json.loads((r/'experiments/steamstub/validation.json').read_text())
meta=v['unpacked'];b=Path(meta['path']).read_bytes()
assert hashlib.sha256(b).hexdigest()=='bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd'
def off(a):
    x=a-meta['image_base']
    s=next(s for s in meta['sections'] if s['rva']<=x<s['rva']+s['file_bytes'])
    return s['file_offset']+x-s['rva']
def u32(a):return struct.unpack_from('<I',b,off(a))[0]
def string(a):
    o=off(a);return b[o:b.index(b'\0',o)].decode('ascii')

registered=[]
for i in range(1000):
    a=0x959c78+i*8;n,f=u32(a),u32(a+4)
    if n==0:break
    registered.append(dict(table_va=hex(a),name=string(n),string_va=hex(n),adapter_va=hex(f)))
else:raise AssertionError('No table terminator')
assert len(registered)==65
uses=json.loads((r/'evidence/startup/native-map.json').read_text())['symbols']
startup_names={s['symbol'] for s in uses if any(u['script']=='root' and u['function'] in ['init','first','title','titlemenu'] for u in s['uses'])}
root_names={s['symbol'] for s in uses if any(u['script']=='root' for u in s['uses'])}
assert len(startup_names)==17 and len(root_names)==35
assert root_names <= {s['name'] for s in registered}
startup=[]
for name in sorted(startup_names):
    row=dict(next(x for x in registered if x['name']==name))
    adapter=next(x for x in v['handlers'] if x['symbol']==name)
    assert adapter['adapter_va']==row['adapter_va']
    row.update(function_argument_va=adapter['function_argument_va'],bridge_va=adapter['bridge_va'])
    startup.append(row)

tasks=[]
for name,getter in [('Advertise',0x43d140),('CharacterLoad',0x43d320),('CompanyLogo',0x43d410),('MemoryCardCheck',0x43d8e0),('Title',0x43e530),('TitleMenu',0x43e620)]:
    data=b[off(getter):off(getter)+0xf0]
    matches=list(re.finditer(b'\xc7\x06(....)',data,re.S))
    assert len(matches)==1
    vt=struct.unpack('<I',matches[0][1])[0]
    exec_row=next(x for x in startup if x['name']=='_Exec'+name)
    target=int(exec_row['function_argument_va'],16);code=b[off(target):off(target)+15]
    assert code[0]==0xe8 and target+5+struct.unpack_from('<i',code,1)[0]==getter
    assert code[5:]==bytes.fromhex('6a008bc88b10ff5220c3')
    assert u32(vt+0x20)==0x4745d0 and u32(vt+0x24)==0x4745e0
    tasks.append(dict(task=name,getter_va=hex(getter),vtable_va=hex(vt),
                      vtable_write_pc=hex(getter+matches[0].start()),
                      slots={hex(i*4):hex(u32(vt+i*4)) for i in range(11)}))

states=[]
for state in range(0x17c,0x1a6):
    index=b[off(0x43fec0)+state-0x17c]
    states.append(dict(state=hex(state),table_index=index,target_va=hex(u32(0x43fe90+4*index))))
strings={hex(a):string(a) for a in [0x8285e0,0x82a0bc,0x6c1d60,0x82bf88,0x82bf98,0x8401dc,0x84026c,0x842830,0x842850,0x842878,0x8428ac,0x830f40,0x830fc4,0x8312d4,0x8312e4,0x83135c,0x831534]}
get_slots=[hex(u32(0x478eac+i*4)) for i in range(8)]
set_slots=[hex(u32(0x47c184+i*4)) for i in range(8)]
result=dict(executable=meta['path'],sha256=meta['sha256'],image_base=hex(meta['image_base']),
            registration_function_va='0x560860',registration_call_pc='0x43fcfc',
            registration_table_va='0x959c78',registration_terminator_va=hex(0x959c78+8*len(registered)),
            registered_count=len(registered),root_native_count=len(root_names),startup_native_count=len(startup),
            registration=registered,startup=startup,tasks=tasks,root_driver_states=states,strings=strings,
            global_get_switch=get_slots,global_set_switch=set_slots,
            caveat='Static code/data evidence. No runtime invocation or visible screen has been observed.')
(out/'mapping.json').write_text(json.dumps(result,indent=2)+'\n')
table=['| Lua name | Adapter VA | Function argument VA | Bridge VA |','| --- | --- | --- | --- |']
table += [f"| `{x['name']}` | `{x['adapter_va']}` | `{x['function_argument_va']}` | `{x['bridge_va']}` |" for x in startup]
(out/'startup-api.md').write_text('\n'.join(table)+'\n')

# Bounded witnesses from the full disassembly; table bytes are deliberately
# excluded where known. Linear disassembly alone does not distinguish code/data.
groups={
 'root-driver':[(0x470ca0,0x470d44),(0x43eae0,0x43eb0f),(0x43ecf0,0x43ed97),(0x43fc90,0x43fe8d)],
 'script-loader':[(0x442120,0x442187),(0x4409b0,0x440aad),(0x560860,0x5608b7),(0x560d00,0x560e1f),(0x560f20,0x560fb2)],
 'scheduler':[(0x560780,0x560853),(0x5608c0,0x560b84),(0x560bc0,0x560cfd),(0x560fd0,0x5611d0),(0x575b40,0x575bd8),(0x575be0,0x575c97)],
 'startup-callbacks':[(0x478750,0x47875f),(0x478790,0x4787af),(0x478810,0x47881f),(0x4788c0,0x4788df),(0x47d310,0x47d332),(0x47d370,0x47d3c2),(0x47d460,0x47d482),(0x47d750,0x47d7b2)],
 'job-control':[(0x40b5c0,0x40b69e),(0x4745d0,0x474607)],
 'state-values':[(0x478e20,0x478eaa),(0x47c080,0x47c182),(0x479120,0x47914b),(0x479500,0x479572),(0x4999f0,0x499a0b),(0x49a560,0x49a59f)],
 'screen-entrypoints':[(0x499ae0,0x499c55),(0x49a690,0x49a8d0),(0x49e070,0x49e163),(0x49e610,0x49e746),(0x4a0e20,0x4a0e69),(0x4a10e0,0x4a119b)]
}
getters=[(int(t['getter_va'],16),int(t['getter_va'],16)+0xf0) for t in tasks]
groups['singleton-getters']=getters
lines={name:[] for name in groups}
for line in (out/'text-disassembly.txt').open():
    m=re.match(r'\s*([0-9a-f]+):\s*(.*)',line)
    if not m or m[2]=='int3':continue
    pc=int(m[1],16)
    for name,ranges in groups.items():
        if any(lo<=pc<hi for lo,hi in ranges):lines[name].append(line.rstrip())
for name,selected in lines.items():
    (out/(name+'.txt')).write_text('Source SHA256: '+meta['sha256']+'\nPreferred VAs; INT3 padding omitted.\n\n'+'\n'.join(selected)+'\n')
(out/'witness-ranges.json').write_text(json.dumps({k:[[hex(a),hex(z)] for a,z in v] for k,v in groups.items()},indent=2)+'\n')
print(json.dumps(dict(registered=len(registered),root_native=len(root_names),startup_native=len(startup),task_vtables=len(tasks),witness_groups=len(groups)),indent=2))
