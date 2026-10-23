#!/usr/bin/env python3
"""One explicit check entry point. No tool installs or game-data writes; CMake fetches pinned Lua and FFmpeg sources."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from tooling import ROOT, format_sources
import tidy


def command(*args, **kwargs):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True, **kwargs)


def configure(build):
    command('cmake', '-S', ROOT, '-B', build, '-DCMAKE_BUILD_TYPE=Debug',
            '-DBUILD_TESTING=ON', '-DBW_WARNINGS_AS_ERRORS=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')


def host(build, jobs):
    configure(build)
    command('cmake', '--build', build, '--parallel', jobs)
    command('ctest', '--test-dir', build, '--output-on-failure')


def policy():
    command(sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_repository_policy.py')
    # Guard the full proposed snapshot, including untracked authored files, without
    # staging in the user's index or writing objects into their repository.
    with tempfile.TemporaryDirectory(prefix='bw-policy-') as tmp:
        temporary = Path(tmp)
        git_dir = subprocess.check_output(['git', 'rev-parse', '--absolute-git-dir'], cwd=ROOT, text=True).strip()
        objects = temporary / 'objects'
        objects.mkdir()
        env = {**os.environ, 'GIT_INDEX_FILE': str(temporary / 'index'),
               'GIT_OBJECT_DIRECTORY': str(objects),
               'GIT_ALTERNATE_OBJECT_DIRECTORIES': str(Path(git_dir) / 'objects')}
        command('git', 'read-tree', 'HEAD', env=env)
        command('git', 'add', '-A', '--', '.', env=env)
        command('git', 'diff', '--cached', '--check', env=env)
        command(sys.executable, 'tools/check_repository.py', '--staged', env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('format', 'host', 'tidy', 'policy', 'all', 'corpus'))
    parser.add_argument('--fix', action='store_true', help='format action only: apply Google formatting')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/host-debug')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 1, 4))
    parser.add_argument('--game', type=Path, help='corpus action only: read-only installation')
    args = parser.parse_args()
    if args.jobs < 1 or (args.fix and args.action != 'format') or (args.game and args.action != 'corpus'):
        parser.error('Invalid option combination; --jobs must be positive')
    if args.action == 'corpus' and not args.game:
        parser.error('corpus requires --game')
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / 'build'):
        parser.error('--build-dir must be under this repository\'s ignored build/')
    if args.action in ('format', 'all'):
        format_sources(args.fix)
    if args.action in ('host', 'all', 'corpus'):
        host(build, args.jobs)
    if args.action == 'tidy':
        configure(build)
    if args.action in ('tidy', 'all'):
        tidy.run(build, args.jobs)
    if args.action in ('policy', 'all'):
        policy()
    if args.action == 'corpus':
        command(sys.executable, 'research/tools/verify_resource_foundation.py', build / 'bw-resource', args.game.resolve())


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Check failed: {error}', file=sys.stderr)
        sys.exit(1)
