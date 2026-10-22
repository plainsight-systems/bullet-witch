#!/usr/bin/env python3
"""Inventory provenance strings in the research copy; never modifies the game.

Run from any directory: python3 /absolute/path/to/engine_provenance.py
Offsets are file offsets. Keyword matches are leads, not engine identification.
"""
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / 'experiments/steamstub/witch.original.exe.unpacked.exe'
data = source.read_bytes()
strings = [dict(file_offset=hex(m.start()), text=m.group().decode('ascii'))
           for m in re.finditer(rb'[\x20-\x7e]{6,}', data)]
path_pattern = re.compile(r'\.(?:cpp|pdb|h)\b', re.I)
term_pattern = re.compile(r'cavia|witch::|lua 5|physx|novodex|engine|unreal|unity|phyre|dinkumware', re.I)
result = {
    'input': str(source.relative_to(root)),
    'sha256': hashlib.sha256(data).hexdigest(),
    'method': 'ASCII printable runs >=6 bytes; regex filters in tools/engine_provenance.py',
    'offset_kind': 'file offset, not virtual address',
    'source_paths': [s for s in strings if '\\' in s['text'] and path_pattern.search(s['text'])],
    'engine_terms': [s for s in strings if term_pattern.search(s['text'])],
}
output = root / 'evidence/engine/binary-provenance-strings.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({'output': str(output), 'sha256': result['sha256'],
                  'source_paths': len(result['source_paths']),
                  'keyword_strings': len(result['engine_terms'])}))
