# Scene lighting and color-pass ownership

This audit follows the reported whole-scene and Special Costume 5 brightness
failure. It does not treat a matching isolated sampler or material program as
proof that the original frame was reconstructed.

## Original command path

`56a520` chooses the visible model's ordinary command destinations.
`56a640` queues coefficient/model commands separately for the four source
chunks. The destination descriptor is the stage render context at `+4148`:
its `+54/+5c/+64/+6c` entries are populated by `45e130` from the main-pass
lists. Source chunks 0 and 1 enter the opaque and alpha-tested lists; chunk 2
enters the blended list; chunk 3 enters the subsequent material-state list.
These are scene-wide lists, not four consecutive draws of each model.

`460920` initializes the main pass. `4614e0` first selects the S program pair
for depth construction; the main color commands select the T pair. Main color
initialization enables depth testing, depth writing and reverse-depth comparison
(`ZFUNC=7`). Before the chunk-2 list, `460920` sets `ZWRITEENABLE=0`;
chunk 3 inherits that state. `565560` disables blending and alpha testing for
opaque work; `565520` enables alpha testing with reference 128 and comparison 7;
`565500` disables alpha testing; `564f50/564f30` enable/disable blending.
Material SRS commands then apply their explicitly authored blend state.

The reconstruction previously submitted every model's chunks consecutively and
enabled depth writing for every non-sky color draw. This differs from the
original scene-wide phase order and late-list depth contract. Correcting those
facts is required, but does not by itself establish the cause of bright opaque
PW5 pixels. Depth prepass optimization, optional original list sorting and the
complete original effect graph remain separate from this specific correction.

## Shader feature provenance

All ordinary model commands constructed by `56a640 -> 421d20` retain the shared
render context `af14b8`. Its `knsS` executor `4308b0` reads the registry pointer
at context `+c` before calling `42f970`. Initializer `443220` explicitly binds
that pointer to `af1500` at instruction `4432e0`;
constructor `42fb50` writes `01010100` at `+1adfc`, enabling the SH byte at
`+1adfd`. The whole-image direct-offset/absolute-address scan found no other
writer of that byte. Adjacent pass executors `42ff30/42ff40` alter directional
and shadow feature bytes, while `42ff50` selects S/T and pixel disable.
They do not disable SH for Alicia. This traces the original producer condition,
not merely the existence of a matching PS123 registry entry. It remains a static
source proof; no original same-scene GPU capture is available.

## Engine ownership

Scene lifetime owns generic spatial fields, authored field multipliers,
directional light, static ambient policy and the emitter registry. Game
composition maps original character/enemy labels to scene-local field identities. A renderable supplies its
model-local group-zero bounds, model world transform and lighting policy. The
engine computes the transformed sample position, original unscaled query radius,
field sample and bounded local-light packet. Body and attached weapon are
independent renderables using the same machinery. Controllers do not own fields,
coefficient packing or light selection.

Static-object ambient and sky-white are different source policies from spatial
character/enemy fields. Preserving this distinction prevents replacing all
models' lighting with a convenient common fallback. See the source producer
audit and the pinned Unreal lighting reference for corresponding scene versus
primitive versus pass responsibilities.

## Implementation and evidence limits

The portable draw contract carries an explicit color phase. Native publication
prepares phase order once, preserving original submission order within a phase;
pose/light/view updates reuse that plan. Geometry and render state remain
retained. Unreal's base-pass mesh processor informs this explicit admission,
resolved-state and publication boundary, not a guessed replacement material
equation or an extra coefficient scale.

Both C++ corpora were queried separately for design and implementation. I.1
makes phase/state and scene/receiver ownership explicit. GPU.6 retains a prepared
submission plan and stable device state rather than rebuilding work per frame.
Raw exports are ignored under `engine/pass-177-lighting-pass-admission` and
`engine/pass-178-color-pass-state`. Existing whole-program instruction evidence
establishes the state executors above.

Visual brightness acceptance stays open until a corrected complete scene has
been compared with original footage. No per-costume brightness scalar, exposure
adjustment, extra division by pi or color-space guess is authorized by this
audit.

## Metering boundary and rejected brightness hypothesis

The bounded trace of `5788b0`, `578950` and `56d6a0` confirms the original
16-tap helper and destination-normalized offsets. It does not establish the
live image contents and complete producer topology on which those helpers ran.
The reconstructed scene therefore cannot claim original frame parity from a
same-input helper witness. Settled native PW5 remained overbright after the
confirmed scene-list/depth-write correction.

At that unresolved metering producer boundary, an ignored diagnostic tested the pinned Unreal Basic
exposure reduction contract documented in `lighting-reference-boundaries.md`:
one 16-by-16 group partitions the valid log-image rectangle, integer-loads
every texel exactly once, reduces log sum and sample weight, then divides once.
The existing pre-bloom full-view log-color target supplies the input. No
center/corner weighting, material scalar or costume exception is introduced.
This was an explicit diagnostic departure, not a claim that the original helper
is incorrect. Original luminance coefficients, natural-log convention,
exposure history/key, gamma and bloom material equations remain unchanged.
The view owns the persistent measurement target and reads only a completed
previous measurement, as before. The settled 160-frame PW5 comparison changed mean RGB by less than one byte
per channel and retained severe glow. The proposed replacement was not retained
in production: the original helper is established, and no independent bug in
its intended source coverage was proven. The reference implementation and
actual captures remain ignored evidence. The subsequent independent raw PW5
material initialization/upload witness is recorded in
`lighting-scene-producer-contract.md`; it matched static bindings, while live
runtime overrides and complete original scene execution remain outside that
witness. Brightness acceptance remains open.


## Live environment correction and native result

The subsequent complete producer trace found the decisive omission: stage
activation enables AREA even though the raw stage record says zero. Camera-eye
region selection, table lookup and scalar channels then update the live stage
record. The world-pass builder republishes those field multipliers every frame.
The source scene-ready state requests one immediate update; later calls blend
normally. See `lighting-scene-producer-contract.md` for literal witnesses.

`scene::Environment` owns region/curve/transition state. The game adapter maps
channels to typed definitions, `scene::SceneLighting` resolves scene/receiver
light packets, and retained render updates consume the complete current values.
The source RGB image operation runs before luminance/bloom/display. Restored live
shadow controls expose finite thresholds above one, which the original accepts;
mode-1 fog ignores the nonfinite linear-distance lanes produced by equal
endpoints, so the portable representation zeros only those unused lanes.

Actual native PW5 and PW0 cave/near-lamp captures now show a large visible
correction without material or costume tuning. An explicit outside-region
placement validates different source values and complete native execution but
faces a wall; it is not an outdoor-scene appearance comparison. The first
non-snapping diagnostic exposed bright startup convergence; the subsequently
recovered scene-ready one-shot snap removes that diagnostic limitation in
production. Final first-frame captures and the combined gate are retained with
this correction's evidence. Physical visual acceptance remains the user's.


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
