#!/usr/bin/env python3
"""Keep ignored artifacts, binary payloads and oversized files out of Git snapshots."""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 1024 * 1024

def git(*args, data=None):
    return subprocess.run(['git', *args], cwd=ROOT, input=data, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout

def check(entries):
    entries = list(set(entries))
    paths = sorted({p for _, _, p in entries})
    if not paths:
        return [], 0
    ignored = subprocess.run(['git', 'check-ignore', '--no-index', '-z', '--stdin'],
                             cwd=ROOT, input=b'\0'.join(paths)+b'\0',
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if ignored.returncode not in (0, 1):
        raise RuntimeError(ignored.stderr.decode())
    ignored_paths = set(ignored.stdout.rstrip(b'\0').split(b'\0')) if ignored.stdout else set()
    failures = []
    checked = {}
    for mode, oid, path in sorted(entries):
        reason = None
        if path in ignored_paths:
            reason = 'ignored/local-only path'
        elif mode not in (b'100644', b'100755'):
            reason = 'link or embedded repository'
        else:
            if oid not in checked:
                size = int(git('cat-file', '-s', oid.decode()))
                if size > MAX_BYTES:
                    checked[oid] = f'larger than {MAX_BYTES} bytes'
                else:
                    blob = git('cat-file', 'blob', oid.decode())
                    try:
                        text = blob.decode('utf-8')
                        checked[oid] = 'binary/control-byte content' if any(ord(c)<32 and c not in '\n\r\t\f' for c in text) else None
                    except UnicodeDecodeError:
                        checked[oid] = 'non-UTF-8/binary content'
            reason = checked[oid]
        if reason:
            failures.append(f'{path.decode(errors="replace")}: {reason}')
    return sorted(set(failures)), len(entries)

def staged():
    for record in git('ls-files', '--stage', '-z').split(b'\0'):
        if record:
            info, path = record.split(b'\t', 1)
            mode, oid, stage = info.split()
            if stage != b'0':
                raise RuntimeError('Resolve index conflicts before checking.')
            yield mode, oid, path

def history(revision):
    # Read each historical tree, not just HEAD or the working directory.
    for commit in git('rev-list', revision).splitlines():
        for record in git('ls-tree', '-r', '-z', commit.decode()).split(b'\0'):
            if record:
                info, path = record.split(b'\t', 1)
                mode, kind, oid = info.split()
                yield mode, oid, path

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--staged', action='store_true')
    modes.add_argument('--history', metavar='REVISION')
    args = parser.parse_args()
    failures, count = check(staged() if args.staged else history(args.history))
    if failures:
        print('Repository content check failed:\n'+'\n'.join(failures))
        raise SystemExit(1)
    print(f'Repository content check passed: {count} file versions, text only, no ignored artifacts.')
