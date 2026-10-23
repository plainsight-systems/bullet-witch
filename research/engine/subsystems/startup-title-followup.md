# Title prerequisites: bounded follow-up

October 22, 2026. Planning evidence for the later launch-screen milestone.
The current immediate task remains final-logo playback; no title implementation
or visual-equivalence claim accompanies this note.

## Resource initialization correction

The earlier [startup dependencies](startup-next-contracts.md) called title's
second initialization gate a worker barrier. The existing instruction exports
support a narrower interpretation: it is another sprite resource-group load.

- `0x499a10` first requests and waits for `sprite.title`.
- It then pushes `0xb`, gets an object through `0x43add0`, and calls `0x474830`.
  Despite that argument, the inspected PC implementation seeds `srand` with
  `GetTickCount`, assigns `rand() % 10` to object `+0x188`, and discards the
  unused stack argument with `RET 4`.
- `0x475320` releases this object's resource group and resets phase `+0x18c`.
- `0x475280` requests the selected name from the table at `0x9595e0` (whose first
  entry is `sprite.loading01`), waits through `0x4ac700`, copies the selected
  index to `+0x184`, and then reports completion. An index of `0xb` bypasses
  loading, but the inspected random selector does not select that value.

Do not introduce a generic worker queue or treat the pushed argument as an
effective loading-screen index based on these calls alone. Table contents and
the selected resource's constructed objects remain the next narrow inspection.
Sources: [title initialization x86](../../evidence/engine/pass-06s/instructions/00499a10.txt),
[selector x86](../../evidence/engine/pass-06s/instructions/00474830.txt),
[loading gate x86](../../evidence/engine/pass-06s/instructions/00475280.txt),
[group reset](../../evidence/engine/pass-06s/functions/00475320.c).

## Minimum title inputs

A read-only metadata inspection of
`data/data/stable/sprite/title/title.cpr` found:

| Record type | Member name | Reported payload bytes |
|---|---|---:|
| USER | `title_info.bin` | 6 |
| DDS | `title_1.png` | 9,587 |
| DDS | `title_2.png` | 80,663 |
| DDS | `title_3.png` | 9,065 |

Archive SHA-256:
`6159f273173ea6fb6df1c1575df52f5f94091f0f763a6034edf70350fde2082c`.
Method: existing `research/tools/cpr_layout.py` `inspect`, metadata only;
[saved evidence](../../evidence/reconstruction/startup-next/title-archive.json).
Member suffixes do not establish a PNG decoder contract: the original record
type is DDS. Compression, texture dimensions, texture construction and the
six-byte title metadata still require interpretation before presentation.

`0x499ae0` opens `data\\movie\\witchLOGO.ssu` on title start and records movie
availability on the title component. `0x499d00` submits three sprite definitions
at `0x95a6a8`, `0x95a6d8`, and `0x95a708`, then the movie texture when available,
then `Version 1.0.5`. `witchLOGO.ssu` is therefore part of the title presentation,
not an additional company-logo table entry. Title update `0x499f10` distinguishes
pressed-input outcome 1 from timeout/movie outcome 2; it separately animates
sprite values and polls fades. The exact constant/table data and animation
helper equations remain needed for a faithful visible title.
Sources: [start](../../evidence/engine/pass-06r/functions/00499ae0.c),
[draw](../../evidence/engine/pass-06r/functions/00499d00.c),
[update](../../evidence/engine/pass-06s/functions/00499f10.c).

## Implementation boundary when title work resumes

Game policy belongs beside `src/game/startup_flows.*` and composition in
`src/game/startup_services.*`; exact file separation can follow the recovered
contracts. Reuse `src/game/startup_playback.*` and the engine video player for
the title movie. Add the minimum decoded sprite definitions/texture resource
construction and renderer-owned sprite submission at their existing architecture
boundaries, with Metal presentation on Mac. The current host's video-only output
cannot draw the three original sprite overlays or font text by itself.

No C++ was changed, new native exports generated, textures extracted, or movie
playback executed for this follow-up. Timing constants, animation equations and
the sprite resource-construction path were not closed in this bounded pass.
