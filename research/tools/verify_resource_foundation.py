#!/usr/bin/env python3
"""Compare the reconstructed native reader with pre-existing corpus evidence.

Only writes beneath research/evidence/reconstruction/. Reads original game files.
CPR reference: existing cpr_layout.inspect (derived from the community BMS reader).
RAT reference: saved layout_probe inventory; names independently reconstructed by
walking each resource's parent chain rather than the native reader's tree traversal.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from cpr_layout import inspect
from layout_probe import dpk
from menu_asset_inventory import png_extent

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def cstring(data, base, offset):
    return data[base + offset:data.index(b'\0', base + offset)].decode('ascii')


def verify_cpr(path, native, saved, counts):
    reference = inspect(path)
    require(reference['sha256'] == saved['sha256'], 'Input hash differs from original inventory')
    require(native['bytes'] == saved['bytes'] == reference['bytes'], 'File length')
    require(len(native['records']) == saved['records'] == len(reference['records']), 'Record count')
    require(native['header_u32_from_0x04'][:15] == reference['header_u32_from_0x04'], 'Header fields')
    require(dict(Counter(r['type'] for r in native['records'])) == saved['types'], 'Record type counts')
    data = path.read_bytes()
    names = reference['names_offset']
    args = 96 + reference['header_u32_from_0x04'][3]
    for a, b in zip(native['records'], reference['records']):
        for key in ('index', 'table_offset', 'type', 'name', 'fields_after_type', 'payload_prefix_hex'):
            require(a[key] == b[key], f'Record {b["index"]}: {key}')
        expected_range = None if b['payload_offset'] is None else {'offset': b['payload_offset'], 'size': b['payload_bytes']}
        # Steam's inline PNG pointer can select a replacement whose extent
        # differs from the unchanged original section size. The independent
        # audit walks chunk framing AND validates CRCs before accepting this
        # interpretation; raw record fields above must still match exactly.
        if (expected_range and b['type'] == 'DDS ' and
                b['fields_after_type'][2] != 0xffffffff and
                data[b['payload_offset']:b['payload_offset'] + 8] == b'\x89PNG\r\n\x1a\n'):
            expected_range['size'] = png_extent(data, b['payload_offset'])
            counts['inline_png_extents'] += 1
        require(a['selected_payload'] == expected_range, 'Selected payload range')
        payload = b'' if expected_range is None else data[b['payload_offset']:b['payload_offset'] + b['payload_bytes']]
        members = dpk(payload)['entries'] if payload.startswith(b'dpk\0') else []
        require(a['dpk_members'] == [{k: e[k] for k in ('name', 'offset', 'stored_bytes', 'declared_bytes')} for e in members], 'DPK members')
        counts['dpk_packages'] += bool(members)
        counts['dpk_members'] += len(members)
        f = b['fields_after_type']
        require(a['publication_name'] == (cstring(data, names, f[1]) if f[1] else None), 'Publication name')
        expected_args = []
        for index in range(f[4], f[4] + f[3]):
            raw = list(struct.unpack_from('<III', data, args + index * 12))
            expected_args.append({'raw': raw, 'name': cstring(data, names, raw[0]) if raw[0] else None,
                                  'string_value': cstring(data, names, raw[2]) if raw[0] and raw[1] == 2 else None})
        require(a['arguments'] == expected_args, 'Argument names/types/values')
        require(a['known_type'] and a['handler_status'] == 'not_implemented', 'Handler status')
        counts['records'] += 1
        counts['arguments'] += len(expected_args)
        counts['inline_references'] += f[2] != 0xffffffff
        counts['publication_names'] += bool(f[1])
    counts['cpr_archives'] += 1


def verify_rat(path, native, saved, counts, archive_refs):
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == saved['sha256'], 'Input hash differs from original inventory')
    require(native['bytes'] == saved['bytes'], 'RAT bytes')
    require([[s['offset'], s['size']] for s in native['sections']] == saved['sections'], 'RAT sections')
    require(len(native['files']) == len(saved['files']), 'RAT file count')
    for a, b in zip(native['files'], saved['files']):
        require(a == {k: b[k] for k in a}, 'RAT file reference')
        target = path.parent / a['relative_path']
        require(target.is_file(), 'Missing archive')
        archive_refs.add(target.resolve())
    require(len(native['resources']) == len(saved['resources']), 'RAT resource count')
    tree, depbase, strings = (saved['sections'][i][0] for i in (0, 3, 5))
    full_names = []
    for entry in saved['resources']:
        components = [entry['name']]
        offset = entry['tree_offset']
        seen = set()
        while offset < 0x80000000:
            require(offset not in seen, 'Reference parent cycle')
            seen.add(offset)
            name, offset = struct.unpack_from('<II', data, tree + offset)
            if name:
                components.append(cstring(data, strings, name))
        full_names.append('.'.join(reversed(components)))
    for i, (a, b) in enumerate(zip(native['resources'], saved['resources'])):
        p = saved['sections'][2][0] + i * 20
        raw = list(struct.unpack_from('<5I', data, p))
        require(a['raw'] == raw and a['name'] == b['name'], 'Raw RAT resource')
        require(a['full_name'] == full_names[i] and a['lookup_matches'], 'Full name and lookup')
        require(a['extra'] == (cstring(data, strings, raw[2]) if raw[2] else None), 'Extra string')
        expected = []
        if raw[4] < 0x80000000:
            p = depbase + raw[4]
            n, = struct.unpack_from('<I', data, p)
            indexes = struct.unpack_from(f'<{n}I', data, p + 4)
            expected.extend({'kind': 'local_index', 'local_index': j, 'name': full_names[j]} for j in indexes)
            p += 4 + n * 4
            n, = struct.unpack_from('<I', data, p)
            offsets = struct.unpack_from(f'<{n}I', data, p + 4)
            expected.extend({'kind': 'named', 'local_index': None, 'name': cstring(data, strings, j)} for j in offsets)
        require(a['dependencies'] == expected, 'Ordered dependencies')
        counts['dependencies'] += len(expected)
    for node in native['namespaces']:
        name, parent, nc, nr = struct.unpack_from('<4I', data, tree + node['offset'])
        require(node['name'] == cstring(data, strings, name) and node['parent'] == parent, 'Namespace name/parent')
        require(node['children'] == list(struct.unpack_from(f'<{nc}I', data, tree + node['offset'] + 16)), 'Child namespace references')
        require(node['resources'] == list(struct.unpack_from(f'<{nr}I', data, tree + node['offset'] + 16 + nc * 4)), 'Namespace membership')
    counts['rat_catalogs'] += 1
    counts['catalog_resources'] += len(native['resources'])
    counts['catalog_files'] += len(native['files'])
    counts['namespace_nodes'] += len(native['namespaces'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('binary', type=Path)
    ap.add_argument('game', type=Path)
    ap.add_argument('--output', type=Path, default=ROOT / 'research/evidence/reconstruction/src-01')
    a = ap.parse_args()
    game, out, binary = a.game.resolve(), a.output.resolve(), a.binary.resolve()
    allowed = (ROOT / 'research/evidence/reconstruction').resolve()
    if not out.is_relative_to(allowed) or out == game or out.is_relative_to(game):
        ap.error('Output must be within research/evidence/reconstruction, outside the game')
    out.mkdir(parents=True, exist_ok=True)
    inventory = out / 'native-inventory.jsonl'
    with inventory.open('w') as stdout, (out / 'native-stderr.txt').open('w') as stderr:
        run = subprocess.run([str(binary), 'inventory', str(game)], stdout=stdout, stderr=stderr,
                             env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}, timeout=180)
    saved_cprs = {row['path']: row for row in json.loads((ROOT / 'research/evidence/layout/cpr-inventory.json').read_text())}
    saved_rats = json.loads((ROOT / 'research/evidence/layout/rat-layout.json').read_text())
    counts, errors, seen, archive_refs = Counter(), [], set(), set()
    with inventory.open() as stream:
        for line in stream:
            row = json.loads(line)
            relative = row['path']
            try:
                require(relative not in seen, 'Duplicate output path')
                seen.add(relative)
                require('error' not in row, row.get('message', 'Native reader failed'))
                if row['kind'] == 'cpr':
                    verify_cpr(game / relative, row['data'], saved_cprs[relative], counts)
                else:
                    verify_rat(game / relative, row['data'], saved_rats[relative], counts, archive_refs)
            except (ValueError, KeyError, IndexError) as e:
                errors.append({'path': relative, 'message': str(e)})
    expected = set(saved_cprs) | set(saved_rats)
    if seen != expected:
        errors.append({'message': 'Inventory path mismatch', 'missing': sorted(expected - seen), 'extra': sorted(seen - expected)})
    if archive_refs != {(game / name).resolve() for name in saved_cprs}:
        errors.append({'message': 'RAT archive references do not cover the CPR corpus exactly'})
    if run.returncode:
        errors.append({'message': f'Native inventory exit code {run.returncode}'})
    summary = {'status': 'passed' if not errors else 'failed', 'counts': dict(counts), 'errors': errors,
               'corpus_reference_sha256': {name: hashlib.sha256((ROOT / 'research/evidence/layout' / name).read_bytes()).hexdigest()
                                           for name in ('cpr-inventory.json', 'rat-layout.json')},
               'native_binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                 for name in (['CMakeLists.txt', 'research/tools/verify_resource_foundation.py', 'tests/resource_cli_test.py'] +
                                              sorted(str(p.relative_to(ROOT)) for folder in ('include', 'src', 'tools', 'tests')
                                                     for p in (ROOT / folder).rglob('*') if p.suffix in ('.hpp', '.cpp')))},
               'method': 'Full corpus: historical input hashes/counts, independent CPR reader, RAT parent-chain names, raw fields, payload ranges/prefixes, arguments, dependencies and lookup self-checks.',
               'assurance': 'Static-derived and corpus-checked. No original-engine execution equivalence claim.'}
    (out / 'verification-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
