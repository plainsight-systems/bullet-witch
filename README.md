# Bullet Witch

Source reconstruction of the Steam PC engine into readable, buildable native
code. Preserve the original behavior, make a direct port to Xbox Series X
Developer Mode, then improve the game and its graphics.

- [Project brief](BRIEF.md)
- [Implementation plan](IMPLEMENTATION.md)
- [Original engine architecture](research/engine/ARCHITECTURE.md)
- [Research index](research/README.md)

Architectural mapping and script recovery are established. Production engine
implementation has not begun. The first milestone is the resource/data foundation.
Future engine source, headers, platform adapters and tests will live alongside
`research/`, in `src/`, `include/`, `platform/` and `tests/`.

## Repository contents

Git contains authored source/tools, documentation and a small reviewed set of
verification summaries. Game assets, extracted scripts, raw decompiler output,
disassembly, Ghidra workspaces, downloaded toolchains and third-party reference
copies remain local and ignored. The `.gitignore` uses explicit exceptions for
reviewed research metadata and authored experiment tools; new generated evidence
is excluded by default. Put any new local game content in `game-data/` or the
appropriate ignored research directory.

The original game installation is external to the repository. A fresh clone will
not contain the analysis inputs or a ready-to-run game. Follow the recorded
research procedures with local inputs and toolchains when reproducing analysis.
Research links into ignored evidence work on the research machine and may be
unavailable in a clone; they are retained as provenance, not promised downloads.

## Before committing or pushing

Enable the versioned hooks in each checkout:

```sh
git config core.hooksPath .githooks
python3 tools/check_repository.py --staged
```

The commit hook checks the staged snapshot. The push hook checks all commits
reachable from the local refs being pushed, so a removed asset still in history
is also rejected. These local hooks can be bypassed; they complement review and
the ignore rules, and are not server-side enforcement. The checker rejects paths
excluded by the current ignore rules, oversized files, binary content and links.
Intentional policy changes require updating/reviewing the rules first.
