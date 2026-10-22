# Steam asset layout and container research

The PC release exposes a resource index, named container records, PNG textures,
compiled Lua scripts, and embedded Direct3D shaders. This is enough to begin
targeted asset and scripting experiments without launching the game. It does not
yet establish a way to run the game on another platform.

Research date: October 21, 2026, America/Phoenix. This extends the
[initial inspection](2026-10-21-steam-inspection.md) of the same `witch.exe` hash.
The scripts read the downloaded files without modifying them. One texture was
copied into `/Users/andyhunter/repositories/tech-demos/bullet-witch/research/extracted` for visual verification.

## Directory map

| Directory | Files | Role supported by the contents |
| --- | ---: | --- |
| `data/data/stable` | 6,529 | Main resources and `stable.rat` index |
| `data/concept` | 119 | Separate PW1–PW5 character resources and `cm*` mission packages |
| `data/menu` | 4 | Menu resources |
| `data/movie` | 18 | ASF movies, all reporting 1280 × 720 video |
| `data/shader` | 2 | Shader names, setup records, and resource index |
| `data/sound` | 26 | 24 music tracks and two wave banks |
| `data/text` | 55 | Binary text resources |

The main `stable` directory separates `ai`, `audio`, `camera`, `customizer`,
`effect`, `graphics`, `jimaku`, `pathevent`, `script`, and `sprite`. Graphics contain
characters (`chara`), environments (`bg`), environment objects (`bgobj`), and
post-processing (`posteffect`). Environment folders include `01`, `02`, `03_a`,
`03_b`, `03_cd`, `03_e`, `04_a`, `04_b`, `04_c`, `05_a`, `05_b`, `06`, `80`, and `81`.
These names are identifiers; their exact correspondence to playable missions is
not established here.

A character such as `PW0` has separate model, texture, motion, shape, control-point,
collision-related, and effect containers. PW1–PW5 have similar layouts under
`concept`, but their costume identities have not been verified in-game.

Evidence: [directory counts and sizes](../evidence/layout/directory-map.json).

## RAT files index the CPR containers

All **25 RAT files** pass the implemented header, section, string, and file-index
checks. Their file tables contain **6,625 references to 6,625 unique CPR paths**,
exactly matching the complete CPR inventory. Every target exists. The tables also
contain **15,262 resource records**. `stable.rat` alone references 6,528 containers
and contains 14,311 resource records.

The observed RAT layout is little-endian. The word at `0x04` equals file size in
all 25 files. Six offset/size pairs occupy `0x10` through `0x3f`. The pair at `0x30`
addresses eight-byte file records: directory-name offset, then filename offset.
Both strings are relative to the string section addressed by the pair at `0x38`.
The pair at `0x20` addresses 20-byte resource records; the first word selects a
resource name, and the fourth selects an entry in the file table. Other fields
and the resource tree remain only partially interpreted.

For example, `data/concept/PW1/PW1.rat` maps `pw1_body_m1_game_psd` to file index 9,
which resolves to `texture/texture.cpr`. This is a direct resource-to-container
relationship, not just a similar filename.

Evidence: [RAT sections, file references, and resource records](../evidence/layout/rat-layout.json).

## CPR containers carry resources and setup calls

A read-only parser, informed by Luigi Auriemma's
[CPR and DPK extraction script](https://mirror.aluigi.org/bms/bullet_witch_cpr.bms),
parsed **all 6,625 CPR files** without name or payload bounds failures. Each has a
`CPR1` signature and the observed little-endian marker. Named 48-byte records begin
at `0x60`; header sizes locate the name table and two payload regions. Some records
describe setup operations rather than extractable data.

| Record type | Count | Observed contents or names |
| --- | ---: | --- |
| `USER` | 10,130 | Headers, animation, collision-related data, scripts, and other resources |
| `DDS ` | 4,630 | PNG payloads, often bearing `.dds` member names |
| `CALL` | 7,969 | Names such as `vfx2.mesh.create`, `vfx.motion.create`, and `ai.add` |
| `DXVB` | 2,163 | Named vertex and shape-vertex buffers |
| `DXIB` | 2,015 | Named index buffers |
| `DDSC` | 25 | Separate texture-related record type; interpretation unresolved |
| `DXPS` | 1,532 | Pixel shader identifiers with zero payload sizes |
| `DXVS` | 1,532 | Vertex shader identifiers with zero payload sizes |

This layout suggests the container loader both exposes resources and dispatches
named setup operations. The meaning and execution of `CALL` arguments still need
confirmation from the executable. Passing the structural checks does not mean all
record semantics have been decoded.

Evidence: [CPR summary](../evidence/layout/cpr-summary.json),
[archive counts and hashes](../evidence/layout/cpr-inventory.json),
[representative records](../evidence/layout/cpr-samples.json), and
[parser](../tools/cpr_layout.py). The external script was read as a format reference,
not executed; its URL, version, and hash are recorded in
[source provenance](../evidence/layout/external-source.json).

## Textures are immediately accessible

All **4,630 `DDS ` records** have PNG signatures in their selected payloads. This
statement covers that record type, not the separate `DDSC` records. Member names
alone are misleading: `PW0_face_m1_game_psd.dds` contains a PNG image.

The PW0 texture container contains 14 named images. I copied its face texture
without conversion, decoded it, and visually verified the 512 × 512 image. The
output is `/Users/andyhunter/repositories/tech-demos/bullet-witch/research/extracted/PW0_face_m1_game_psd.png`.
The PW0 body texture header reports 1024 × 1024. The remaining images have not all
been decoded or checked for PNG integrity.

Evidence: [extraction identity and hash](../evidence/layout/texture-preview.json).

## Mesh and animation data are separated but still proprietary

PW0's `model/mesh.cpr` contains 31 records: a `header.bin` beginning with `2XFV`, a
`texlist.bin` beginning with `TLST`, `motion.bil` and `motion._bl`, nine ordinary
vertex buffers, four shape-vertex buffers with matching offset data, nine index
buffers, and a `vfx2.mesh.create` call.

The animation bundle contains named resources such as `pw0_at_stagger._ba` and
`pw0_cannon._ba`, paired with `vfx.motion.create` records. Its 322 records comprise
161 payloads and 161 setup calls. Names and buffer boundaries make a focused
viewer experiment possible, but vertex declarations, bone transforms, animation
encoding, and material interpretation remain unresolved. No mesh has been
reconstructed or rendered.

The PW0 hit container exposes `ehit.bin` plus `vfx.hit.create2`; this does not by
itself establish the underlying collision representation.

## Level packages contain compiled Lua

All **56 containers in `stable/script`** contain a DPK package. The packages expose
344 members: 57 `.lub`, 190 `.settbl`, 47 `.bgb`, 24 `.arc`, 22 `.hide`, and four
`.bin` files. All members have equal stored and declared sizes, and the inspected
payloads are directly readable without a decompression step.

All 57 `.lub` members begin with the Lua signature and version byte `0x50`, matching
the [Lua 5.0 binary format](https://www.lua.org/source/5.0/lundump.h.html). The sampled
header also describes little-endian data and four-byte integers, sizes, and
instructions. A matching header does not establish that every opcode or runtime
binding is standard Lua; no script has been executed or decompiled.

`root.cpr` contains `root.dpk`, which contains `root.lub`. Its strings include
`titlemenu`, `stage_select`, `concept_mission`, `_ExecCompanyLogo`, and
`_ExecMemoryCardCheck`. These suggest a script-controlled startup and menu flow,
with behavior delegated to native engine functions.

`st01_00.cpr` contains an 11-member DPK with `st01_00.lub`, `event.bin`, setting
tables for enemies, environment, reverb, magic, and score, plus path, hiding, and
background-object data. Signatures include `STBL`, `STMANAGE`, `arcs`, and `BGOB`.
The separation offers a way to investigate level composition and scripting
independently of the renderer. The AI package separately contains named `.amb`
resources with an `ai` prefix and `ai.add` records; these have not been identified
as Lua.

Evidence: [DPK members, hashes, and selected Lua strings](../evidence/layout/script-dpk-layout.json).

## Shader names are external and compiled candidates are embedded

`shaders.cpr` contains 3,830 records: 1,532 `DXPS`, 1,532 `DXVS`, and 766
`cgl.shader.setup` calls. All records have zero payload sizes under the observed
container layout. The names form 766 variant IDs, each with `_S_xpu`, `_S_xvu`,
`_T_xpu`, and `_T_xvu` entries. All 766 IDs also appear as null-terminated strings
in `witch.exe`.

A conservative executable scan found **541 shader candidates** with a shader
version token, an initial `CTAB` comment, a traversable token sequence, and an end
token: **473 pixel shaders and 68 vertex shaders**, all reporting Shader Model 3.0.
The scan uses Microsoft's documented
[version](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/version-token),
[instruction](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/instruction-token),
[comment](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/comment-token),
and [end](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/end-token)
token layouts. It is a structural check, not full shader validation or proof of
runtime use, and it intentionally misses shaders without the initial `CTAB` pattern.

This supports investigating an executable-resident shader library. The mapping
from the 766 external variant IDs to the embedded candidates remains unproven;
the different counts should not be interpreted as missing shaders. Shader source
has not been recovered.

Evidence: [candidate offsets, lengths, profiles, and hashes](../evidence/layout/embedded-shader-candidates.json).

## Media and build provenance

FFprobe successfully inspected all 18 movies and 24 music tracks. Every movie is
ASF with 1280 × 720 video and WMA Pro audio: 17 report `wmv3`, one reports `vc1`.
All music tracks report XWMA containers with `wmav2` audio at 44,100 Hz, stereo.
This probes metadata; it is not full playback verification. The two XWB banks
still need entry and codec analysis.

Local downloader metadata filenames identify game depot **696181**, manifest
**6465548775586257852**, plus depots 228985 and 228990. A Steam app build ID has not
been established. Evidence: [media probes](../evidence/layout/media-probes.json) and
[manifest identifiers](../evidence/layout/depot-manifests.json).

## Recommended next experiment

Trace one small path from the resource index through a CPR payload to its native
consumer. The startup Lua package is a useful first candidate: inspect its bytecode
and identify the engine functions used for the title screen. A separate PW0 mesh
viewer is another bounded experiment now that textures and buffer boundaries are
accessible. The owner should choose which direction best informs the revised brief.

Neither experiment requires committing to Xbox 360 recompilation. The PC release
contains substantial reusable data and scripting, while the native engine and
platform requirements remain the central feasibility questions.

## Reproduce the evidence

Run from the project root. Python uses only its standard library; the supplementary
probe also calls the installed FFprobe.

```sh
python3 research/tools/cpr_layout.py /Users/andyhunter/Games/BulletWitch-Steam research/evidence/layout
python3 research/tools/layout_probe.py /Users/andyhunter/Games/BulletWitch-Steam research/evidence/layout
```

The parsers describe this downloaded PC build. They are research tools with partial
format coverage, not general-purpose extractors or importers.
