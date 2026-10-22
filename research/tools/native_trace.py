#!/usr/bin/env python3
"""Select address ranges or literal operand references from saved LLVM output.
This is a navigation helper, not a function-boundary or control-flow analyzer.
Addresses are preferred virtual addresses. INT3 padding is omitted in ranges.
"""
import argparse
from pathlib import Path
import re

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('mode',choices=['ranges','refs'])
ap.add_argument('addresses',nargs='+',help='hex start:end ranges or hex addresses')
args=ap.parse_args()
path=Path(__file__).resolve().parents[1]/'evidence/native-startup/text-disassembly.txt'
if args.mode=='ranges':
    spans=[tuple(int(x,16) for x in a.split(':')) for a in args.addresses]
else:
    needles=[re.compile(r'0x'+format(int(a,16),'x')+r'(?![0-9a-f])') for a in args.addresses]
for line in path.open():
    m=re.match(r'\s*([0-9a-f]+):\s*(.*)',line)
    if not m:continue
    addr=int(m[1],16);ins=m[2]
    if args.mode=='ranges':
        if any(start<=addr<end for start,end in spans) and ins!='int3':print(line.rstrip())
    elif any(n.search(ins.split('#')[0]) for n in needles):print(line.rstrip())
