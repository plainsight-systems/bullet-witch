#!/usr/bin/env python3
"""Hash-bound original-root integration; read-only game, ignored research output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from cpr_layout import inspect
from layout_probe import dpk

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('game', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'research/evidence/reconstruction/startup')
    args = parser.parse_args()
    game, binary, output = args.game.resolve(), args.binary.resolve(), args.output.resolve()
    if not output.is_relative_to(ROOT / 'research/evidence/reconstruction') or output.is_relative_to(game):
        parser.error('Output must stay under research/evidence/reconstruction, outside the game')
    path = game / 'data/data/stable/script/root.cpr'
    record = next(row for row in inspect(path)['records'] if row['name'] == 'root.dpk')
    data = path.read_bytes()[record['payload_offset']:record['payload_offset'] + record['payload_bytes']]
    member = next(row for row in dpk(data)['entries'] if row['name'] == 'root.lub')
    original = json.loads((ROOT / 'research/evidence/script-corpus/inventory.json').read_text())
    expected = next(row for row in original if row['name'] == 'root.lub')['sha256']
    if member['sha256'] != expected:
        raise ValueError('Root chunk differs from the independently inventoried original')
    run = subprocess.run([str(binary), str(game)], capture_output=True, text=True, timeout=30,
                         env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
    output.mkdir(parents=True, exist_ok=True)
    (output / 'stdout.txt').write_text(run.stdout)
    (output / 'stderr.txt').write_text(run.stderr)
    result = json.loads(run.stdout) if run.returncode == 0 else {'status': 'failed', 'exit_code': run.returncode}
    result.update(root_sha256=expected, archive_sha256=sha(path), binary_sha256=sha(binary),
                  source_sha256={str(p.relative_to(ROOT)): sha(p) for folder in ('include', 'src', 'cmake')
                                 for p in sorted((ROOT / folder).rglob('*')) if p.is_file()},
                  tooling='Pinned Lua 5.0.2 + documented host patches; production resource/task/bridge/root modules; fixture route plus reconstructed startup session with controlled media observations')
    (output / 'verification-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return run.returncode

if __name__ == '__main__':
    raise SystemExit(main())
