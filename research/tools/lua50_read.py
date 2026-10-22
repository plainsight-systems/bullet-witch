#!/usr/bin/env python3
"""Static Lua 5.0 reader for the observed 32-bit little-endian Bullet Witch chunks.
Reference: https://www.lua.org/source/5.0/{lundump.c,lopcodes.h,llimits.h}.html
No Lua code is executed. This is a scoped decoder, not a full VM verifier.
"""
import json
import struct
OPCODES='MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE SELF ADD SUB MUL DIV POW UNM NOT CONCAT JMP EQ LT LE TEST CALL TAILCALL RETURN FORLOOP TFORLOOP TFORPREP SETLIST SETLISTO CLOSE CLOSURE'.split()

class Reader:
    def __init__(self,data,string_size=4,instruction_size=4):
        self.data=data;self.pos=0;self.string_size=string_size;self.instruction_size=instruction_size
    def take(self,n):
        if n<0 or self.pos+n>len(self.data):raise ValueError(f'Truncated chunk at {self.pos:#x}, need {n}')
        b=self.data[self.pos:self.pos+n];self.pos+=n;return b
    def byte(self):return self.take(1)[0]
    def integer(self):return struct.unpack('<I',self.take(4))[0]
    def count(self):
        n=self.integer()
        if n>len(self.data):raise ValueError('Invalid count')
        return n
    def string(self):
        n=int.from_bytes(self.take(self.string_size),'little')
        if n>len(self.data):raise ValueError('Invalid string length')
        if not n:return None
        b=self.take(n)
        if b[-1]!=0:raise ValueError('Unterminated Lua string')
        return b[:-1].decode('utf-8',errors='backslashreplace')
    def function(self,path='main',depth=0):
        if depth>100:raise ValueError('Prototype nesting limit')
        f={'id':path,'file_offset':self.pos,'source':self.string(),'line_defined':self.integer(),
           'upvalue_count':self.byte(),'parameter_count':self.byte(),'vararg':self.byte(),'stack_size':self.byte()}
        f['lines']=[self.integer() for _ in range(self.count())]
        f['locals']=[{'name':self.string(),'start_pc':self.integer(),'end_pc':self.integer()} for _ in range(self.count())]
        f['upvalue_names']=[self.string() for _ in range(self.count())]
        constants=[]
        for _ in range(self.count()):
            t=self.byte()
            if t==0:v=None
            elif t==3:v=struct.unpack('<d',self.take(8))[0]
            elif t==4:v=self.string()
            else:raise ValueError(f'Unsupported constant type {t}')
            constants.append(v)
        f['constants']=constants
        f['children']=[self.function(path+'.'+str(i),depth+1) for i in range(self.count())]
        f['code_offset']=self.pos+4
        code=[]
        for pc in range(1,self.count()+1):
            word=int.from_bytes(self.take(self.instruction_size),'little');op=word&63
            if op>=len(OPCODES):raise ValueError(f'Unknown opcode {op}')
            bx=(word>>6)&0x3ffff
            ins={'pc':pc,'word':word,'op':OPCODES[op],'A':word>>24,'B':(word>>15)&511,'C':(word>>6)&511,'Bx':bx,'sBx':bx-131071}
            if ins['op'] in ['LOADK','GETGLOBAL','SETGLOBAL']:
                if bx>=len(constants):raise ValueError('Constant index out of range')
                ins['constant']=constants[bx]
            if ins['op']=='CLOSURE' and bx>=len(f['children']):raise ValueError('Child prototype index out of range')
            code.append(ins)
        for ins in code:
            if ins['op'] in ['JMP','FORLOOP','TFORPREP']:
                target=ins['pc']+1+ins['sBx']
                if not 1<=target<=len(code):raise ValueError('Jump outside function')
                ins['target_pc']=target
        f['code']=code
        # Infer names only from adjacent top-level closure/global assignments.
        for prev,cur in zip(code,code[1:]):
            if prev['op']=='CLOSURE' and cur['op']=='SETGLOBAL' and prev['A']==cur['A']:
                f['children'][prev['Bx']]['global_name']=cur['constant']
        return f


def parse(data):
    r=Reader(data)
    header=r.take(14)
    expected=bytes.fromhex('1b4c756150010404040608090908')
    if header!=expected:raise ValueError('Unsupported Lua header '+header.hex())
    test=struct.unpack('<d',r.take(8))[0]
    if int(test)!=int(3.14159265358979323846E7):raise ValueError('Number format mismatch')
    root=r.function()
    if r.pos!=len(data):raise ValueError(f'Trailing bytes: {len(data)-r.pos}')
    return {'header_hex':header.hex(),'number_test':test,'bytes_consumed':r.pos,'root':root}


def listing(f):
    lines=[f"FUNCTION {f.get('global_name',f['id'])} [{f['id']}] parameters={f['parameter_count']} upvalues={f['upvalue_count']} stack={f['stack_size']}",
           'CONSTANTS '+json.dumps(f['constants'],ensure_ascii=False)]
    def rk(n):return 'R'+str(n) if n<250 else 'K'+str(n-250)+'='+repr(f['constants'][n-250])
    for i in f['code']:
        op=i['op'];a,b,c=i['A'],i['B'],i['C'];note=''
        if 'constant'in i:note=repr(i['constant'])
        if 'target_pc'in i:note='goto '+str(i['target_pc'])
        if op=='CLOSURE':note='prototype '+f['children'][i['Bx']]['id']
        if op in ['EQ','LT','LE','ADD','SUB','MUL','DIV','POW','SETTABLE']:note=f'{rk(b)}, {rk(c)}'
        if op in ['GETTABLE','SELF']:note=rk(c)
        lines.append(f"{i['pc']:4} {op:10} A={a:<3} B={b:<3} C={c:<3} Bx={i['Bx']:<6} ; {note}")
    for child in f['children']:lines.extend(['',*listing(child)])
    return lines
