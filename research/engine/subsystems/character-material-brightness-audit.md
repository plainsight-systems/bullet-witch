# Character material brightness audit

The physical review of Special Costume 5 reported an excessively bright character.
The menu's compatibility ID 5 selects `pw5`; it is not the fifth item including
the original costume. This remains a visual acceptance failure under investigation.
No brightness, exposure, tint, color-space or normal correction has been selected
from an image comparison.

## Original contract and evidence boundary

PW5 body materials use stored selector `0x22490` and variant 1. Original registry
entry `00822690100`, including enabled fog and SH bits, selects vertex program
453 and pixel program 123. The reconstructed common material shader is a
translation shared with other recovered families; it is not the original binary.
Each semantic branch must therefore be checked against its selected original
program rather than assuming that one validated family establishes the rest.

| Boundary | Original evidence | Current audit |
|---|---|---|
| Appearance selection | menu costume index, `prepare_player` publication | Confirmed Special Costume 5 → PW5 |
| Vertex position/normal/tangent | program 453, SHORT4N and four influences | Actual PW5 declarations and VS453 audited: FLOAT3, SHORT4N normal/tangent, HALF2 UV, UBYTE4 indices/normalized weights; original influence order and TBN match |
| Spatial coefficients | `565d70/5656d0/5659f0/566f90/567af0` | Existing C++ translation has literal x86 differential evidence on 142 actual points; this is not an invented interpolation |
| Model callback | `5e4430`, group center `56a8f0`, copy `42bbc0/545000` | Living player constructor installs observer5e4430; it selects context+93f8 (`_cl`), samples transformed group center and copies without normalization |
| Packing/upload | `5453a0`, `4277b0` | Original constants/index order match current seven-vector packing; upload is pixel constants 9–15 |
| Texture data | Actual PW5 PNG/metadata and selected texture references | Encoded textures contain IHDR/pHYs/IDAT/IEND; no gAMA, sRGB, cHRM or ICC chunks. Actual body AMOC is present, not a white fallback |
| Sampler baseline | `451770`, material sampler overrides | Direct and generic sampler/state paths audited: no sRGB decode; source D3DX input flags and current UNORM interpretation agree |
| Material constants | PW5 original CCOL/FRSR/LUMI/REFP/SPCC/ENVP records | Actual values retained in ignored native census; no per-costume multiplier |
| Pixel material | programs 74/123/412 | Two recovered operand omissions corrected; independent PS123 register replay matches current PW5 output on actual supplied inputs |
| Display transfer | `56d6a0`, final program `844bc8` | Original RGBA16F target and explicit final gamma shader match; no sRGB write or additional exposure correction found. Whole-game parity remains unproven |

The prior source verification established the spatial sampler and direct-light
selection, not complete PW5 material-to-display visual parity. That distinction
was missing from the readiness conclusion.

## Confirmed material discrepancy

Pixel program 123 samples AMOC into `r6`, then multiplies Fresnel response by
`r6.y` and `reflectParam.x`. Its red channel separately multiplies ambient plus
SH. Programs 74 and 412 retain the same separation. The previous shader sampled
red for ambient/SH but omitted green from reflection weight. The corrected shader
fetches RG once and applies the channels to their respective source terms. This is an original
instruction omission, not a proposed art adjustment.

Controlled native isolation does **not** establish this omission as the primary
brightness cause: eliminating reflection entirely leaves the bright PW5 body
nearly unchanged in the initial cave view. Eliminating only actor SH makes it
dark. The initial actual direct-light packet is empty by the recovered influence
radius, so nearby point lights do not explain that initial view. The green-channel
correction must not be presented as resolving the reported glow without further
evidence.

## Actual-data diagnostic

Ignored evidence is organized in `character-light/brightness/`: a production
Stage/PlayerActor/WorldRenderer offscreen probe, independent build manifest,
original PW5 material/texture census and exact shader variants. Baseline, no-SH,
no-reflection, no-direct and source-green-mask variants preserve the same pose,
camera, source settings and display pipeline. Diagnostic shader changes do not
modify production source. PW0 supplies a same-view appearance control.

The actual body AMOC (`PW5_body_g_game_psd`) red/green means are approximately
0.586/0.399 over the complete texture. Those statistics are evidence of bound
resource content, not factors to apply to the rendered character. Per-pixel UV,
material input, normal, coefficient, reflection and output transfer remain the
necessary end-to-end comparison; averaged texture statistics cannot prove it.

Both C++ guidance corpora were read for design and implementation: I.1 keeps
pixel interpretation/channel contracts explicit; GPU.1 keeps sampled resources
resident and reuses one texture fetch for distinct channels. This audit does not
authorize a new color-space conversion or an appearance-based coefficient scale.


## Completed source boundary audit and corrections

The second PS123 omission was directional shadow visibility in the cubemap
response. Original `mad_sat r1.x, r3.w, r5.w, c18.w` follows a `cmp` that carries
visibility for positive directional facing. The translated actor/background receiver response now uses
`saturate(facing * visibility + reflectParam.w)`. Back-facing surfaces retain
the authored reflection floor. The independent final review caught that this
equation must not be shared with static environment programs: PS225/226/228/229
use `saturate(facing + reflectParam.w)` and contain no shadow sampler. The
prepared `environment_shadow` material feature now selects the original equation
for each family, independently of the existing direct diffuse/specular shadow
policy. Actor/background PS40/43/46/47/104/109/117/123/437/438 enable the feature;
static environment programs leave it false. No-AMOC families still use identity
reflection occlusion without sampling slot5. This is separate from AMOC green; neither term
alters SH irradiance or adds an exposure adjustment.

The initial unit-intensity claim was incomplete: `568030` initializes the light
owner to one, but `45e130` later writes stage-record `+0x1c/+0x20` into character/
enemy owner `+4`. `45ae00` resolves `witch/stage/<stage>`; initializer
`45cbe0→45ff80→5f9c00` does not modify either field. Literal x86 execution of
`45eed4..45eef0` with installed `st04_c` records writes one to both owners. Other
stages differ (`st01`: 0.1/0.2; `st03_a`: 1/5), so the format decoder now retains
both values and the player sampling call receives the character multiplier.
`567af0` multiplies file intensity by owner intensity first, then each sampled
RGB coefficient. The input is finite but not clamped or normalized. The enemy
value is decoded without inventing an admitted enemy rendering owner. This
raw-record correction alone had no brightness effect. That was not the live
scene contract: the subsequent activation/AREA trace below supersedes any
inference that the character owner remains one throughout `st04_c`.

The material constant upload modifier is also resolved: original initializer
`401800` writes unit scale and zero bias for all 256 vertex and 256 pixel
register entries; `4432f4` installs the table consumed by `4303f0→547e70→546dc0`.
A literal x86 initializer witness verifies all 512 entries. The current living,
unenchanted player retains unit indirect RGB: visibility/proximity consumers
change alpha while preserving RGB. USER resource relocation/publication does
not preprocess SHLT floats. No additional brightness factor is justified here.

Independent source audits of sampler/output states and VS453 found no further
correction. In particular, no source sRGB texture/write enable was found in the
direct or generic state routes, and original output already applies explicit
gamma. The actual source SHORT4N normal/tangent streams contain no -32768 edge;
non-unit summed skin weights are preserved as in the source. TBN uses
`cross(tangent, normal)` and does not normalize the reconstructed mapped normal.
These checks do not authorize a physically based replacement or texture retint.

## Actual-input pixel-program verification and remaining limit

The ignored `brightness/ps123_reference.py` mechanically translates the original
PS123 disassembly's material arithmetic, register swizzles/masks, dynamic light
loop and final reflection/fog composition into a separate Metal fragment. It
runs against the same production PW5 interpolants, textures, material constants,
SH and direct-light packets. It intentionally shares the existing shadow
visibility helper and final HDR/display stage; it does **not** execute the
original D3D bytecode on original hardware. The independent output and vertex
audits cover those boundaries separately, without claiming complete gameplay
parity.

At both initial and near-lamp poses, the register translation and corrected
production output are byte-identical over the complete 768×768 image. A separate
magenta coverage diagnostic confirms this branch actually shades the body and
wings; equality is not an unexecuted-reference artifact. Evidence includes
`ps123-render-comparison.json`, `pw5-original-ps123`, `pw5-corrected`, and
`pw5-ps123-coverage` under `character-light/brightness`. The bright body remains
visible in the source-derived fragment result. This establishes material
translation equivalence **on the supplied inputs**, not that the user's reported
brightness is acceptable or that the complete original gameplay frame has been
reproduced. No arbitrary reduction is applied to make that conclusion look fixed.

Synthetic native coverage now verifies AMOC green zero suppresses reflection
while red stays one, and green one restores it in the actual PW5 family. Format
and sampler tests verify independent character/enemy values and live owner
intensity, including zero and signed inputs. Native resource lifetime and
retained-light tests remain in the existing fixture. Original output/declaration
source evidence lives under `character-light/output-contract` and
`character-light/vertex-contract`; it remains ignored proprietary research.


The same rebuilt probe also retains `pw5-before-operands` using the saved prior
shader, so pose/scheduler changes from concurrent actor work do not contaminate
the material comparison. The full-scene difference includes the source cubemap
visibility correction on other supported reflective materials; it is not a
character-only brightness delta. The character remains visibly bright before
and after. The PS123 replay comparison uses the corrected common vertex/output
stages in both cases and has exactly zero differing pixels at both views.


The final cross-family native regression holds material, cubemap and lighting
inputs fixed while changing a real caster (visibility one/zero). Static PS225
reflection remains unchanged; actor PS123 changes and retains the authored 0.25
reflection floor. With zero facing, both families retain the same floor whether
shadowed or unshadowed. This verifies the explicit family distinction through
classification, native layout publication and GPU consumption. Source review
provenance is `character-light/final-source-review/audit.md`; the correction
introduces no scene-name or costume-name branch.

## Settled exposure diagnostic

The final ignored diagnostic performs 480 actual Metal presentations: 240 at the
initial fixed PW5 scene and 240 at the fixed near-lamp scene. Exposure history
fills at frame127. Both settled images retain the severe bright appearance;
initial/settled output differs by at most one byte per channel. Replaying
original `5712f0`/`571060` instructions with the captured readback and elapsed-time
inputs matches all 479 adapted outputs at float-bit level and all history
counts. This rules out an unsettled-capture explanation for these inputs; it
does not establish original GPU luminance, scene inputs, first-frame texture
initialization, global history lifetime or whole-game clock integration.

Evidence and exact instrumentation bounds are in ignored
`research/evidence/reconstruction/character-light/exposure-sequence/README.md`
and `report.json`; settled captures are `pw5/frame-239.png` and
`pw5/frame-479.png`. No production exposure/gamma or material tuning was applied.
The final read-only lighting-input provenance audit remains separate, and the
user's brightness complaint is not marked resolved.

## After consecutive exposure validation: remaining input provenance

The separate stationary exposure investigation completed 480 actual native
presentations. Literal original `5712f0/571060` execution agrees at float-bit
level for all 479 completed-readback updates. The severe bright appearance
persists after the history fills. This eliminates the proposed first-frame-only
adaptation explanation for the supplied frames; it does not establish that the
original game supplies the same HDR scene or luminance input. Details and exact
limits are retained in `character-light/exposure-sequence/README.md`.

A bounded follow-up reviewed the remaining producer/caller chain without changing
production code or repeating the sampler/fragment/exposure experiments:

- `42a720` reads group zero's admitted definition center at `+0x30`. `56a8f0`
  sets its homogeneous coordinate to one and transforms it with the matrix
  returned by `42a570`, the core model's `+0xa0` matrix. `56b850` composes the
  wrapper transform, applies the original render half-turn and publishes this
  core matrix through `42bb10`. Thus lighting samples the rendered model bounds,
  not the actor sphere center, pelvis bone, or an unrotated wrapper origin.
  Current `PlayerActor::model_transform` and `StageInspection::player_lighting`
  follow this contract. This establishes the algorithm and field provenance;
  it does not independently observe the original game's live model matrix.
- The actual concept script `cm01_00` explicitly loads riff `st04_c`, character
  light `st04_c`, and stage `st04_c`. Its `start` function supplies placement,
  object activation, visibility/fade, input/game pause, goals/traps and BGM. It
  contains no direct light-field or intensity override. Source visibility
  changes preserve indirect RGB; the known placed-light producers from object
  activation are already supplied. No additional omitted lighting producer was
  demonstrated by this bounded script/start audit. This is not an assertion
  that the entire original mission runtime has been executed by the port.
- The known character callback/resource choice, activation multipliers,
  coefficient packing, actual material selection and upload equations remain
  as documented above. No newly demonstrated field/scale/normal/texture/exposure
  discrepancy emerged. Re-executing the same routines with reconstructed inputs
  would not independently validate the original live inputs.

The useful next discriminating evidence is a same-scene original runtime frame,
preferably with a graphics/debug capture: selected body resource and group-zero
header; actual model matrix and sampled world center; selected `_cl` resource
identity and live multiplier; nine sampled coefficients and final pixel
constants9–15; actual vertex/pixel program IDs, textures/material constants and
render-pass/output parameters. Comparing the first differing value with the
reconstruction would locate a producer or publication error. A matching original
screenshot can establish an appearance discrepancy but cannot by itself identify
the responsible input; unrelated stages/platform versions cannot establish this
contract. No such original live-state capture is currently present in the
evidence. This bounded pass had not yet found a correction. The subsequent
activation and live-environment trace below supersedes that stopping point.


## Runtime environment producer correction

The missing boundary was activation followed by spatial environment updates.
Original `45ae00→45cbe0→45ff80→5f9c00` changes CSTM byte `+24` from the raw
archive zero to one. `43eea0` performs the initial update and `475600` supplies
the camera eye to `4829d0`. Region selection, section-table row/key lookup and
scalar animation choose the environment target. Ordinary updates blend
continuous fields by 1/32 new + 31/32 current and snap differences below 0.0001;
flags and integer fields publish immediately. The scene-ready startup path
`43f16e→4829c0` writes the one-shot snap request; the next environment query
consumes and clears it. Production requests that exact initial snap and uses
ordinary blending thereafter. There is no arbitrary warm-up count.

On actual `st04_c` inputs the cave target supplies character intensity 0.005,
ambient RGB zero, exposure key 0.13 and gamma 2.1. The exterior target supplies
0.057, zero, 0.06 and 2.5 respectively. These values come from installed
FLAN/STBL/AREA resources, not code constants. `45e130` is a per-frame world-pass
builder: it republishes current CSTM character/enemy intensity before model
submission. Thus isolated samplers operating with a supplied multiplier of one
could match perfectly while the scene was badly wrong.

The same update selects a pre-tone color operation and changes fog, directional
lighting, sky and shadow parameters. Production now prepares a scene Environment
owner for region/curve/transition state, maps the original schema at the game
boundary and publishes live engine lighting and render values. The source raw
shadow alignment threshold may exceed one; AREA targets around31–39 are branch
controls, not cosine values. The previous normalized-range rejection was not in
the original and has been removed while retaining finite validation.

See `lighting-scene-producer-contract.md` and the ignored
`character-light/output-contract/frame-composition-audit.md` evidence record
for literal original witnesses and exact remaining boundaries. Native visual
acceptance is recorded after the integrated environment and pre-tone pass run;
these source corrections alone are not a claim of physical acceptance.


The first scene-ready capture exposed a second, separate publication defect:
native output bypassed the source luminance bounds until a previous readback
existed. Original `461f00→5712f0` applies those bounds even with empty history.
The native first submission now bounds its actual current GPU measurement for
both bloom and final transfer; later completed measurements drive temporal
history normally. No-history mode retains the original raw path. No fixed seed,
extra readback or fabricated warm-up is introduced. The source initial texture
contents remain unknown, so this claims the recovered bounds and consumer
contract, not identical original first-frame timing.

Final native PW5 and PW0 `startup-0.png` captures are correctly shaded from the
first presentation, with the initial flash removed. Both probes completed with
exit zero. Captures at frames 0, 1 and 10 are retained under ignored evidence
`character-light/pass-correction/pw5-first-bounded/` and
`character-light/pass-correction/pw0-first-bounded/`. Physical review remains
open; the capture comparison does not establish full-game visual parity.

Final integrated validation: `python3 scripts/check.py all` completed with exit
zero (build, six CTest targets, fatal clang-tidy, policy and repository content
checks). The gate log is retained at
`character-light/pass-correction/area-full-gate.log`. This work remains
uncommitted for review.


## User acceptance — October 24, 2026

The user accepted the current slice for commit, noting that lighting may be
slightly too dark but is acceptable for this first slice. This supersedes the
open physical-acceptance status in the investigation entries above. No further
brightness tuning was applied. The observation remains a future source/footage
comparison point; acceptance does not establish full-game visual parity or
resolve the explicitly unproven source initialization details.
