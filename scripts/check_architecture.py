#!/usr/bin/env python3
"""Enforce authored engine/game/native/developer include direction."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {'.hpp', '.h', '.cpp', '.mm', '.inc', '.metal', '.c'}


def check():
    errors = []
    checked = 0
    for base in ('include/bw', 'src'):
        for path in sorted((ROOT / base).rglob('*')):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            source = path.relative_to(ROOT).as_posix()
            text = path.read_text()
            physics_backend = source.startswith('src/engine/physics/jolt/')
            # Provider syntax is private even if an accidental CMake include
            # path makes it compile in a controller or portable header.
            jolt_headers = re.findall(r'^\s*#\s*include\s*[<"](Jolt/[^>"]+)[>"]', text, re.M)
            if not physics_backend and (jolt_headers or re.search(r'\bJPH\s*::', text)):
                errors.append(f'{source}: Jolt provider syntax escapes private physics backend')
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
                if physics_backend and name.startswith('Jolt/'):
                    continue
                candidates = (path.parent / name, ROOT / 'include' / name, ROOT / 'src' / name)
                target = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
                # Pinned Lua/FACT C headers are provided by their selected targets.
                if target is None:
                    if name in {'lua.h', 'lauxlib.h', 'lualib.h', 'FAudio_internal.h', 'FACT_internal.h'}:
                        continue
                    errors.append(f'{source}: unresolved authored include {name}')
                    continue
                destination = target.relative_to(ROOT).as_posix()
                checked += 1
                if source.startswith('include/bw/') and destination.startswith('src/'):
                    errors.append(f'{source}: portable contract imports private {destination}')
                if source.startswith('src/engine/') and destination.startswith(('src/game/', 'src/platform/', 'src/developer/')):
                    errors.append(f'{source}: engine imports product/backend {destination}')
                if source.startswith('src/game/') and destination.startswith(('src/platform/', 'src/developer/', 'src/engine/')):
                    errors.append(f'{source}: game bypasses portable contract with {destination}')
                if source.startswith('src/platform/') and not source.endswith('/main.cpp') and destination.startswith(('src/game/', 'src/developer/', 'src/engine/')):
                    errors.append(f'{source}: native adapter bypasses portable contract with {destination}')
                if source.startswith('src/game/actors/animation/') and destination.startswith('src/game/actors/alicia/'):
                    errors.append(f'{source}: common publication adapter imports Alicia policy')
    if errors:
        raise ValueError('\n'.join(errors))
    print(f'Architecture include directions: {checked} resolved edges passed')


if __name__ == '__main__':
    check()
