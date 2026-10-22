# Dedicated repository setup

October 22, 2026.

The project moved from `/Users/andyhunter/repositories/tech-demos/bullet-witch`
to `/Users/andyhunter/repositories/bullet-witch`. Neither the previous project
nor its parent had Git history. The new checkout uses branch `main`.

The old project path is now a local symlink to the new location. This preserves
historical absolute links and saved analysis paths without copying the 2.3 GB
research tree or editing historical command records. Use the new path for future
work. `engine_slice.py` now rebases a historical command and Java environment to
the current research root before exporting a new slice, preventing accidental
reuse of an old output directory after relocation.

The active reconstruction plan moved to root-level
[IMPLEMENTATION.md](../../IMPLEMENTATION.md). Its previous research path is a
forwarding note. Current brief/map navigation points to the root plan.

## Git contents

The ignore rules exclude extracted game content, vendor toolchains and generated
analysis output. Evidence, source-reference copies and experimental workspaces
are excluded by default, with explicit exceptions for reviewed summaries,
provenance records and authored analysis tools. Model/container/executable format
rules also protect against common game files being placed outside those folders.
No blanket `*.json`, `*.c` or `*.lua` rule hides future authored source.

[Repository checker](../../tools/check_repository.py) rejects ignored paths even
when force-added, non-text blobs, files over 1 MiB, symlinks and embedded
repositories. The commit hook checks the index; the push hook checks reachable
history, including deleted files. Hooks are enabled locally with
`core.hooksPath=.githooks`; a fresh clone must enable them as documented in the
[root README](../../README.md). These are local safeguards, not remote enforcement.

## Verification

- All five repository-policy tests passed: allowed source/metadata, forced
  artifact addition, staged blob versus changed working file, size/link rejection,
  and a synthetic removed asset still rejected by the actual push hook.
- Initial staged content passed the repository checker; no game assets,
  executables, raw decompiler output or vendor workspaces were included.
- Relocated analysis image still passes its original SHA-256 check.
- Historical Ghidra command paths rebase to existing files/directories.
- Current implementation/map links and the historical path alias resolve.

Commands:

```sh
python3 -m unittest discover -s tests -v
python3 tools/check_repository.py --staged
```

The game installation remains external and unchanged. No remote is configured
and no push was performed. Source reconstruction implementation is still the
next phase; this change establishes the repository and content policy.
