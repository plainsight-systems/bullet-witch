# Cavia engine research: reusable format work and contemporary documentation

October 22, 2026. [Architecture](../engine/ARCHITECTURE.md).

## Result

Existing community work substantially accelerates the model/resource side of our
engine map. The strongest find is Rich Whitehouse's Bullet Witch Python importer,
which also supports Zegapain XOR. We used its structural descriptions immediately
against the Steam PW0 model, successfully traversing the complete model-header
chunk stream and locating the PC vertex declaration encoding.

The [source catalog](../sources/community-reverse-engineering.md) records original
author links, downloaded source, pinned revisions and scope. A recovered
[Softimage interview](../sources/softimage-bullet-witch.md) adds contemporary
firsthand detail about Cavia's custom graphics production pipeline.

## PC comparison

Input, opened read-only:
`/Users/andyhunter/Games/BulletWitch-Steam/data/data/stable/graphics/chara/PW0/model/mesh.cpr`

SHA-256: `a6d963e51df0fa59d919b2bfa4ba65e1b581cdb111e59f2b3bb1f6eb6c280655`.

| Structure | Local result |
|---|---|
| `header.bin` | 16,884 bytes; traversal reaches exact end; two CNKH chunks |
| Vertex declarations and ordinary bindings | Nine of each; all declarations terminate correctly |
| Index bindings | Nine IDX records and nine archive index buffers |
| Shape bindings | Four VTXS records; referenced buffer lengths divide by their strides |
| Groups / draw records | 14 DGRP groups and 46 DIP records |
| Skinning palette metadata | 46 CM1R maps; target bone indices in range |
| Skeleton | 125 records, 104 bytes each; exact payload length and valid parent ranges |
| Texture names | 14 names; all parsed texture-reference indices in range |

This is a structural comparison, not a complete semantic decoding of every chunk.
Unknown leaf payloads are skipped using checked sizes. No native game animation
has been decoded, and a rendered mesh has not yet verified coordinates or skinning.

The PC declaration is **not** the console declaration with its byte order changed.
After the four-byte VDCL prefix, it contains eight-byte Direct3D 9 vertex elements
ending in the expected terminator. The console importer uses twelve-byte elements.
The PC also stores PNGs in its DDS records; console texture tiling/conversion does
not apply to those payloads. Details and a first vertex layout are in the
[model-data map](../engine/subsystems/model-data.md).

## Method and reproduction

1. Read the author-distributed importer as source; execute no Noesis binary.
2. Use our existing CPR parser to locate bounded member payloads.
3. Walk the model metadata with an independent Python probe, adjusting endian
   and declaration handling based on the observed PC bytes.
4. Check chunk boundaries, declaration terminators, skeleton/texture lengths,
   palette and texture reference ranges, and vertex-buffer stride divisibility.
5. Save structured evidence and source provenance inside research.

From the project root:

```sh
python3 research/tools/compare_noesis_layout.py \
  /Users/andyhunter/Games/BulletWitch-Steam/data/data/stable/graphics/chara/PW0/model/mesh.cpr \
  research/evidence/community-research/pw0-noesis-comparison.json
```

[Probe](../tools/compare_noesis_layout.py) ·
[Results](../evidence/community-research/pw0-noesis-comparison.json) ·
[Work log](../logs/2026-10-22-community-research.md)

## Mapping priorities resulting from this work

- Use the recovered model metadata to connect `vfx2.mesh.create` to buffer creation,
  material binding and draw submission in the native executable.
- Build a focused PC mesh reader using its actual declarations, skeleton and draw
  records. A rendered PW0 mesh is the next concrete graphics milestone.
- Follow `vfx.motion.create` and `_ba` payload consumers for animation decoding;
  the Noesis procedural example does not supply that decoder.
- Use the original NieR texture source for comparison when relevant. Its observed
  container vocabulary differs; Zegapain's shared importer is the closer format lead.
- Keep historical engine naming, exact exporter implementation and the cited
  CG World issue as local unresolved research items. They do not gate engine mapping.
