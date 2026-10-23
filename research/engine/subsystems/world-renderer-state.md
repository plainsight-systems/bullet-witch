# Original renderer state inputs

October 23, 2026. This source reconstruction supports the first static-world
inspection pass. Constructor defaults, queued state, authored stage state and
rendered output are different evidence levels. Selecting a supported original
shader for inspection does not establish the original complete stage pass.

## Shader selection and indirect color

`0x42fb50` initializes the shader registry's pair index to 1, pixel-disable byte
to 0, and feature bytes at offsets `0x1adfd`, `0x1adfe`, `0x1adff` to 1. Fog mode
at `0x1ae00` starts at 0. The instruction witness writes packed `0x01010100`;
Ghidra's pointer-looking spelling for that immediate is not an actual pointer.
`0x42f970` combines these values with the material flags before lookup. In
particular, the enabled byte at `+0x1adfd` adds `0x200`, selecting spherical
harmonic lighting. Omitting that bit selected the wrong sky program during the
first reconstruction. A registry name alone does not prove that a given stage
draw selected it.

Queued executors `0x42ff30` and `0x42ff40` copy command byte `+0x18` to feature
bytes `0x1adff` and `0x1adfe`. `0x42ff50` copies pair selection and pixel-disable
from a separate command. `0x460c60` queues enabled values for both feature bytes;
that state must not be replaced by an assumed universal disabled-light default.

Model-state constructor `0x42a8f0` initializes its indirect-color vector at
`+0x860` to `(1,1,1,1)`, read from executable constant `0x6c2e50`. Setter
`0x42b980` replaces this vector. Builder `0x421e90` copies it to draw command
`+0x500`; executor `0x427220` uploads it to pixel constant 0. This establishes a
recoverable constructor value, not the final authored lighting of `st04_c`.

Global initialization `0x451460` also uploads `(1,1,1,1)` to both pixel constants
0 and 1. Raw `0x451482`–`0x451489` copies `0x6c2e50` to stack `+0x10`;
the calls ending at `0x4515d9` and `0x4515f2` both address that same vector
(the first LEA accounts for an already-pushed argument). This establishes the
secondary-base modulation baseline used by original two-base programs. It is
separate from the model owner's indirect-color-A override.

Global command `0x457130` also initializes vertex constant 0 to `(1,1,1,1)`
and vertex constant 1 to `(0,0,0,0)`, the position scale/offset pair. Raw
instructions at `0x45714e`–`0x457186` distinguish the two stack vectors;
Ghidra incorrectly prints the same local variable for both uploads. Model-specific
overrides must still be checked rather than trusting that pseudocode spelling.

Vertex constants 4–7 receive texture-offset vectors from global `0x1b59850` in
`0x451460` (instruction witness `0x451548`–`0x4515a1`). That address falls in
the PE section's zero-filled tail; inspected direct references only read or pass
it. This supports the zero baseline used when a selected material supplies no
texture-offset override, without claiming that no indirect writer can exist.

## Sampler baseline

`0x451770` explicitly initializes sampler state. It is called by global shader
state initialization `0x451460`, wrapped by queued executor `0x457130`.
The original D3D9 sampler identifiers come from the local SDK reference headers.

| Samplers | U/V/W addressing | Magnification / minification / mip filter |
|---|---|---|
| 0–6 | wrap / wrap / wrap | linear / linear / linear |
| 7 | clamp / clamp / clamp | linear / linear / linear |
| 8–11 | clamp / clamp / wrap | linear / linear / linear |
| 12 | wrap / wrap / wrap | point / point / point |
| 13–14 | native address value 6 / 6 / wrap | point / point / point |
| 15 | clamp / clamp / wrap | linear / linear / linear |

The first translated textured shader uses slots 0 and 3. Its known baseline is
therefore wrap with linear filtering. The selected materials' `SSSM` entries
restore MAXMIPLEVEL to zero for `LENV`, sampler 7, which this shader does not use.
Other sampler overrides and mip construction remain part
of the selected pass/resource contract; this table does not establish every
stage sampler's final state.

Four candidate materials carry `SRS` entries selecting `BTNO` table entry 1.
Resolver `0x547370`, executor `0x5471e0`, and helper `0x545db0` resolve packed
state `0x01220625` to source-alpha / inverse-source-alpha blending with ADD.
The initial opaque pipeline excludes their 60 draws; 104 of the candidate
164 draws remain. See [stage content decoding](stage-content-decoding.md).

## Inspection camera coordinates

`LuaNative__SetPlayerPosition` at `0x47c470` copies the script's XYZ values
directly into position and matrix translation. The fourth argument is converted
from degrees to radians before calling `0x435110`; its rotation-axis convention
is not established here. No coordinate scaling occurs in this adapter. The initial inspection camera now reconstructs the authored preset, script
yaw and static clearance from these inputs; see
[player-camera-initialization.md](player-camera-initialization.md). The script
pose remains an explicit bootstrap until actor startup owns the live pose.

## Fog construction

Fog executor `0x4276d0` sets the registry fog mode through global `0xb0c300`,
unless the command mode is 3. It uploads color from command `+0x50` to pixel
constant 39. Helper `0x425160` derives pixel constants 40–42 from distance,
height and secondary-height ranges and the view's height offset. These are
actual equations, not a generic modern fog replacement.

Default command builder `0x421c30` uses white color, distance 20000–30000,
height ranges -30000 to -20000, and unit multipliers. The general builder
`0x422280` copies supplied parameters. Stage composition `0x460300` supplies
values from the environment owner at global `0x1b65f24`, with independent enable
flags for distance and height contributions and explicit coordinate scaling for
one command path. Consequently the default builder cannot be presented as the
authored stage environment. The static inspection pass must state any omitted
stage environment contributions.

## Reproduction and evidence

Use the existing hash-checked analysis project:

```sh
python3 research/tools/engine_slice.py pass-08-world-globals \
  0042fb50 0042ff30 0042ff40 0042ff50 0042b980 004250e0 00421aa0
python3 research/tools/engine_slice.py pass-08-world-state \
  00451770 00412ae0 00412c00 00408160 00420360 0044e5d0 0044e620 0044e670
python3 research/tools/engine_slice.py pass-08-world-environment \
  004169d0 00460300 00457130 0044e690 0044e640 0044e5f0
```

The export helper creates a new named directory; use a new pass name when
repeating an existing export. Generated pseudocode, instruction witnesses and
references remain ignored under `research/evidence/engine/`. Bounded PE constant
reads and render-state call-site navigation are saved under
`research/evidence/reconstruction/stage/`. The input executable SHA-256 is
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
No original binary, texture, shader bytecode or recovered implementation is
included in tracked source.

## Cull baseline evidence boundary

D3D9 render state `0x16` is CULLMODE. Its documented default is
counterclockwise culling, value 3. The original main and sky pass setup
(`0x460920`, `0x460ed0`) selects depth/alpha behavior without setting culling;
selected main/cave material records contain no cull override. The queued state
executors `0x565560`, `0x565520`, `0x565500`, `0x564f50` and `0x564f30` only
change alpha test/blending. Particle group builder `0x513ed0` explicitly queues
CULLMODE=3 after each group; output passes `0x570a10` and `0x571bc0` also set 3.
Some sprite/effect helpers set NONE (1), so their state is not evidence that
world rendering should universally disable culling. A CCW inspection baseline
is supported by the API default and these restores, while complete cross-pass
ordering remains unresolved. This is not a claim that every original world
entry point resets that state explicitly.

Primary API reference: [Microsoft D3D9 render states](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3drenderstatetype)
and [culling values](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcull).
Original queued helper exports are retained under ignored engine pass 27.

## Stage vertex-color and position packing audit

The selected stage does not use packed `D3DCOLOR` vertex elements. Its retained
PC declarations use `UBYTE4N` (type 8) for both illumination `COLOR2` and the
three main-model draws carrying modulation `COLOR0`. This distinction matters:
Microsoft's [D3DDECLTYPE contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3ddecltype)
normalizes `UBYTE4N` in byte order, whereas type 4 interprets a packed `D3DCOLOR`
and expands it to RGBA. Applying a blanket red/blue swap to these stage vertices
would change the authored colors incorrectly.

The production declaration lookup requires type 8 for these inputs. It does not
silently accept type 4 as the same representation. Recovered vertex shader 456
consumes `COLOR2` without a channel permutation and computes
`exp2(alpha * 255 - 128) * rgb`; the Metal illumination path uses the same
normalization and expression. The ordinary model illumination draw counts are
1228 main, 261 background, 359 background2, 431 detail1, 934 detail2, 394 detail3,
and 382 cave. The remaining ordinary draws either use other material inputs or
have no illumination color attribute. Sky has no color attribute.

All position declarations in the stage/sky census are `FLOAT3` (type 2). The
original shader expands this to `(x,y,z,1)`, applies VS constants 0/1 as scale
and offset, then uses the local-screen/world matrices. The global command's
one/zero baseline is established above. The material resolver `0x547370`
recognizes `CF2I` semantic `SOFS` as the two-register override starting at VS0;
`CF1I` material semantics do not write VS0 or VS1. The current decoder rejects
`CF2I`, so successful parsing of this entire selected stage/sky corpus also
establishes that it contains no such authored override. Together with the
ordinary world constructor's identity placement (see
[stage-content-decoding.md](stage-content-decoding.md)), this supports the
current unscaled baked stage positions; this audit found no missing conversion
between stage units and world units. Other models containing a scale/offset
record must acquire an explicit consumer before rendering.

Evidence is retained under `research/evidence/reconstruction/stage/`:
`all-materials.jsonl`, `all_material_census.cpp`, and the derived
`vertex-color-position-audit.json`. Shader operations are in
`research/evidence/reconstruction/world-renderer/shader-456.txt`; material
semantic resolution is in
`research/evidence/engine/pass-08-world-materials/functions/00547370.c`.
This was a targeted fidelity check; no format or shader behavior was changed.

## Sky constant upload: bounded negative findings

A fresh production-decoder probe of `sky04_c` confirms nine `CF1I` records:
`PARF=(0,0,0,0)`, `CCOL=(1,1,1,1)`, `FRSA/FRSB/FRSR=(1,0,1,0)`,
`LUMI=(0,0,0,1)`, `REFP=(1,0,0,1)`, `SPCC=(0,0,0,0)`, and
`ENVP=(1,1,1,1)`. There is no `AMBI` record. The original material patch flag
and register slots are zero, so the native resolver runs before first use.
The optional `model.sky04_c.matanim.list.nocompress` lookup is absent.
The raw record bytes and exact optional-lookup result are saved in
`research/evidence/reconstruction/stage/sky-constant-audit.log` alongside its
small manual CPU probe source.

The semantic resolver `0x547370` maps `LUMI` to PS16 and `AMBI` to PS17.
`0x546dc0` reads the stored float4 directly, applies the register's multiplier
and additive bias, and uploads it; there is no exponent decoding or hidden
alpha-to-RGB conversion in that callback. Raw assembly corrects an important
inferred ownership detail: `0x4305a8` supplies the world renderer's `+0x14`
pointer as ECX to `0x547e70`, which preserves it for `0x546dc0`. Initialization
`0x4432f4` sets this pointer to `0x00b11b20`. Static initializer `0x401800`
constructs 256 vertex and 256 pixel scale/bias entries and sets every scale
float4 to one and every bias float4 to zero. Direct references to the whole
`[0x00b11b20,0x00b15b20)` table range in the retained disassembly are confined
to that initializer and pointer binding. This establishes the neutral initial
state; it does not claim an exhaustive exclusion of indirect writers.
The witness is `research/evidence/reconstruction/world-renderer/constant-scale-bias-witness.txt`.

The generic model-constructor callbacks at vtables `0x827fa4` and `0x827fbc`
are not material scale/bias setters: they resolve to `0x545ea0`/`0x545f80`,
which upload camera-relative directional/local light packs starting at PS21
or an indexed light register. Their defaults therefore do not explain a
nonzero PS16/PS17 sky value.

The installed `data/shader/shaders.cpr` remains hash-identical to the original
layout census (SHA-256 `f4efdebdbea3347d853e9137fa4de3452fd03bf3ebc1741117b02c188aa69487`).
Its payload regions both start at EOF, byte275120. All four records for
`06e00000100` and all four for the corrected SH-enabled `06e00200100` have
zero-sized payloads; no replacement shader bytecode exists in those records
to compare against embedded PS362/PS363. The installed file scan
finds `shader.rat` and `shaders.cpr`, with no `.xpu` or shader-source `.fx`
files. The exact metadata witness is
`research/evidence/reconstruction/world-renderer/sky-shader-package-audit.json`.

Neither recovered `cm01_00` nor `test_map_st04_c` explicitly sets sky color,
ambient light, or material constants during `start()`. Their loading calls
supply character light, sky and stage names. The production mission then sets
background objects/player pose and resumes the game before fades. These
negative checks do not establish that a black sky is intended. The later
selector trace below resolves the missing contribution through original SH
lighting; it does not alter material PS16/PS17 or substitute brightness.

The immediate native postload path was also checked without claiming that
`start()` has been executed by the reconstruction. In mission driver
`0x43eea0`, the loaded-to-start branch reaches `0x43cbf0`/`0x485960` (singleton
time/fade state), `0x496650` (release/reset earlier sequence buffers),
`0x482000` (clear/gameover callback registration), `0x43c630` (script-work
slots), and `0x43c1e0` (profile/player/checkpoint restoration). The inspected
first-level initialization does not upload a sky material color. The additional
`0x48ecd0` path resets its queue/work arrays; deeper consumers were not treated
as proved irrelevant. Existing exports are under `pass-mission-load-first`,
`pass-03a`, and `pass-14-placement-registration`; the latter two small reset
bodies are also visible directly in the retained whole-program disassembly.

## Sky and ordinary-model harmonic lighting closure

The sky material audit above examined effective key `06e00000100`, which maps
to VS448/PS362. That key omitted the registry's enabled `0x200` feature. Original
lookup `0x42f970` reads registry byte `+0x1adfd` and ORs that bit before hashing
and resolving either installed or embedded variants. Constructor `0x42fb50`
sets it to one with packed write `0x01010100` at `+0x1adfc`; actual registry
storage begins at `0x00af1500`. The bounded whole-image reference scan found no
other direct offset or absolute-address writer to that byte. Adjacent queued
executors change the light/shadow bytes and pair selection, not this SH byte.
The corrected sky key `06e00200100` maps to VS448/PS363, which consumes the
harmonic constants. The previous inference that the sky's white SH was unused
therefore described the incorrectly selected program, not the original draw.

Sky virtual `0x5e3660` conditionally tail-jumps to `0x56b440`; Ghidra includes
that destination in its pseudocode body. At `0x56b504`–`0x56b521` it constructs
uniform SH from `(1,1,1,1)` and copies nine coefficient vectors to model context
`+0x300`. `0x42bbc0` explicitly adds `0x300` before its tail-call to `0x545000`;
that fixed 144-byte copy cannot modify the separate ambient vector at `+0x2e0`.
Submission `0x56a7a0` queues the SH data through `0x4223b0` before model draws.
The sky's zero `LUMI` and absent `AMBI` are therefore consistent with its separate
original harmonic contribution. No material color override is needed.

Ordinary static stage objects have a different input owner. Their final
constructor `0x5e0eb0` replaces the base vtable with `0x850e28`, whose `+0x18`
slot is `0x5e46d0`. Render entry `0x5e37a0` delegates to `0x56b550`, which invokes
that virtual before `0x56a520`/`0x56a640` queues the model. The override reads the
first three floats of the selected `witch/stage/<name>` environment, appends
alpha one, constructs uniform SH and replaces the model coefficients. It does
not use the white constants from the superseded base-class virtual. The
`st04_c` values are authored RGB `(1,1,1)`; the next stored float is `0.5` and is
not the SH alpha. `StageEnvironment::ambient_color` preserves those three values
for render composition, validating finiteness without clamping HDR components.
The synthetic decoder case checks unequal HDR RGB and nonfinite rejection.
Applying these coefficients must follow each original shader's equation,
including its interaction with vertex illumination; it is not an arbitrary
additive ambient term.

Instruction and vtable witnesses are retained in
`research/evidence/reconstruction/world-renderer/sh-feature-context-witness.txt`.
The ordinary override is also exported under
`pass-36-view-projection/functions/005e46d0.c`; submission is retained under
`pass-44-stage-area-context`. No ambient-copy mutation or material scale/bias
workaround was introduced by this investigation.
