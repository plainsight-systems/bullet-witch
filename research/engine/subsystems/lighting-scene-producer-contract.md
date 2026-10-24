# Lighting scene producer contract

This October 24, 2026 bounded source audit separates scene lighting ownership
from player behavior and checks the coordinate frame before considering a
replacement algorithm. The initial raw-stage gate conclusion was incorrect; the activation and live AREA producer audit below supersedes it.
It does not close the reported brightness failure or establish an original live
frame. The broader [brightness audit](character-material-brightness-audit.md)
records that unresolved result.

## Recovered ownership and lifetime

| Owner | Original behavior | Engine contract |
| --- | --- | --- |
| Scene light fields | `441160` resolves `charalight.<stage>_cl` and `_el`; `568030` retains the selected resource and initializes owner multipliers. Context `+93f8` and `+9404` are separate owners. | Scene retains prepared immutable fields for its lifetime; character and enemy selection remain explicit. |
| Frame publication | `45e130` publishes the current stage record `+1c/+20` to the two field owners on each world pass. AREA updates can change those values first. | Scene receives live multipliers independently of field data. |
| Renderable spatial lighting | `5e4430` obtains group-zero authored center through `42a720`, transforms it through `56a8f0`, samples the character field, selects local lights, and publishes coefficients. | Register local geometry bounds and field policy, then update the submitted model transform. Selection and publication belong to scene lighting. |
| Attached weapon | Weapon model installs the same lighting callback; it samples its own transformed bounds. | A weapon is a separate renderable with its own transform, not a reused player-lighting packet. |
| Static stage model | Override `5e46d0` constructs uniform harmonics from the selected stage's first three floats, with alpha one, and `5f6e60` copies the global direct-light list. | Static authored ambient and view-selected direct lights are distinct from per-renderable character selection. |
| Sky | `56b440` constructs uniform white harmonics. | Sky has its source policy; it must not accidentally enter the character field. |
| Local emitters | `5f6e80` reads the scene emitter registry, queries the renderable center and unscaled group-zero radius, selects/ranks lights and publishes the direct packet. | Engine scene lighting owns registered emitters and queries. Entity owners only publish emitter data/transforms. |
| Draw submission | `56b550` invokes the model lighting callback before submission. `56a7a0` queues copied SH before the model chunks. | Lighting updates must precede the corresponding retained draw publication. Controller ticks do not own shader constants. |

The currently admitted path has disabled overflow-to-SH in the installed setup.
That does not justify changing spatial coefficients or folding direct lights into
ambient. Authored local lights and their admission/ranking remain as recorded in
[character light sampling](character-light-sampling.md).

A portable engine lighting interface should preserve these independent policies
and lifetimes. Game composition supplies authored resources, registration and
transforms; player movement, aiming and animation controllers have no lighting
query responsibility. GPU upload is the renderer consumer, not the field owner.
Moving this responsibility out of the current `StageInspection::player_lighting`
composition is an architectural correction; it alone cannot fix brightness.

## Static direct-light list: a separate recovered boundary

The static callback is not universally a no-local-light path. `5e46d0` calls
`5f6e60` before its uniform-SH publication. `5f6e60` clears the model list and
uses `5f6de0` to copy up to four nonnull entries from global `01b65e58`. The list
producer is `5f70f0`, called by `457490` before scene render submission:

1. `457490` reads the camera position from context `+80/+84/+88` and constructs
   the camera frustum with `40a860` from matrix `+50` and projection `+e0`.
2. `5f70f0` traverses enabled scene emitters. `546510` prepares source light
   values and a visibility radius using contribution threshold `0.05`;
   `40b2a0` tests that sphere against four side planes and the far plane. There
   is no near-plane test, and touching a plane is accepted.
3. `5f6070` orders that list by each emitter's previously published view depth
   (`+124`), ascending. Constructors `5f6200/63e0/65c0` initialize it to zero.
   Its linked-list comb sort uses integer `gap * 10 / 13` and the 9/10-to-11
   gap adjustment. Equal depths do not directly swap. This history determines
   tie ordering before luminance selection.
4. `546110` calculates a tighter influence radius at threshold plus selection
   margin (`0.20` here). A second frustum test removes failing emitters from
   the active actor list and records rejection as negative maximum float.
   Survivors record `(far - near + far_plane_distance) - influence_radius`.
   Crucially, instructions `5f73c5..5f73cf` append the light pointer to the
   global candidate array even after its active-list node was removed. Thus
   global static selection and actor selection have different candidate sets.
5. It ranks the candidate list with `5461f0`, using camera position, fixed
   receiver radius `0.01`, and capacity four. With no candidates it clears all
   four global pointers. This is not the actor query's additional
   `distance(center, emitter) - model_radius < influence_radius` admission.
6. Static models consume the shared result through `5f6e60`; actor and weapon
   callbacks separately perform their per-model spatial queries.

The selected cave static families (VS454/456/458 and PS221/225/226/228/229)
do not consume `lightsListL`. The currently admitted untextured quads do:
raw flags zero, fog contribution `0x800000` and harmonics contribution `0x200`
resolve to effective key `00800200000`, T VS447/PS16. PS16's instructions
44–66 consume the local-light list. The previously omitted global packet was
therefore an actual admitted consumer gap. This is not evidence that adding
direct energy fixes the overly bright character; metering and complete output
must be measured separately.

`LocalLights::select_view` now prepares both source candidate sets once per
published view, retains the previous-depth scalar, and returns the global
packet. `select_active` queries only the tighter active set and rejects before
view publication. The independent raw `select` operation remains for explicitly
supplied emitter-set witnesses; scene receivers use the prepared operation.
Prepared index/scratch arrays allocate only at construction. Invalid view input
leaves prior publication intact. Scene emitter replacement constructs a new
owner and publishes its view before replacing existing state, resetting newly
registered depth history as the original constructors do.

`math::make_perspective_frustum` reconstructs the five planes from camera world
axes and projection extents, retaining source `far - near` for depth ordering.
Its independent literal-x86 witness compares original `40a860` results with a
rotated, translated asymmetric camera. A second witness executes original
`5f6070` and its original linked-list splice instructions over 400 synthetic
lists with duplicate depths; the reconstructed ordered indices match. These
are algorithm witnesses, not an original game-frame or performance claim.
Scripts/results remain in ignored `character-light/scene-producers/`.

Focused in-process contracts exercise view versus actor admission, the retained
global candidate after the tighter cull, prior-depth ordering of equal
luminance, required view publication, invalid-view retention and bounded
allocation. They do not infer visual parity from synthetic data.

## World coordinate frame, independently checked

The source coefficient and normal consumers agree on world axes:

1. `42bb10` stores the submitted model matrix at core-model `+a0`. `42a570`
   returns that matrix without conversion. `56a8f0` forces the geometry center's
   homogeneous coordinate to one and transforms it by this matrix. It does not
   use the camera, actor sphere center, or a selected animation bone.
2. `5e4430` passes that world position to `567af0`. The callback copy
   `42bbc0→545000`, queued `4223b0`, and upload `4277b0→5453a0` retain the sampled
   coefficient basis. Packing applies fixed SH polynomial factors/swizzles;
   these routines do not rotate the coefficients into camera or model axes.
3. `42ffc0` publishes the transposed submitted model matrix to vertex constants
   16–19, subtracting camera XYZ only from translation. Camera orientation
   participates in the separate clip matrix at constants 12–15.
4. VS453 transforms the normal and tangent through the joint palette, then
   performs direction dot-products with constants 16–18. PS123 consequently
   evaluates its SH polynomial with world-oriented directions. No camera-space
   SH coefficient rotation is required.

A fresh literal-x86 witness executes `42ffc0` with deliberately different model
and view rotations, intercepting only the D3D constant-upload API. The normal
basis upload remains the model basis while the clip upload changes with the view.
This is an independent check of the original publication routine, not a
factorization of our completed animation palette. Script and result live under
ignored `character-light/scene-producers/source_frame_witness.py` and
`source-frame-witness.json`. It uses synthetic inputs and is explicitly not an
original runtime capture.

Current world-space skin palettes followed by translation-only `world_rows`
express the same intended frame. The earlier actual-vertex factoring witness
only proves that algebra on supplied palettes; this audit does not promote it
to independent verification of the original live pose evaluator's matrices.
No direction-axis flip, inverse-transpose insertion or coefficient rotation is
supported by this source trace.

## Scene gates and remaining limits

The earlier conclusion that raw `st04_c` byte `+24 == 0` disables AREA
updates was wrong. The audit stopped at stored asset bytes instead of following
activation. `45ae00 → 45cbe0 → 45ff80 → 5f9c00` writes that byte to one.
Original `4829d0` therefore enters its AREA/STBL/FLAN update branch. This is a
concrete missing runtime input producer, not an opaque lighting equation.

A literal-x86 witness runs original AREA/STBL/FLAN constructors, activation and
`482910 → 475d80 → 4ac280 → 4829d0` on actual installed `st04_c` bytes. With
the source first-publication flag set, the initial eye selects area id 1,
row 1 and scalar frame 1. It publishes ambient RGB zero, character field
intensity `0.005`, enemy intensity one, exposure key `0.13`, gamma `2.1`, bloom
key `0.4`, bloom scale `0.1` and threshold `12`. These differ substantially from
stored defaults. An outside point selects row/frame zero, whose character/enemy
intensities are `0.057/0.1`, exposure key `0.06` and gamma `2.5`.

The SH multipliers are live. `473260` invokes `45e130`, then `4e7370` for scene
submission. At the end of that world-pass builder, `45eed4..45eef0` copies the
current CSTM `+1c/+20` into the current field owners' `+4`. Calling this a
one-time activation in the earlier audit obscured that per-pass publication.
The original owner initializes to one, but that value is not the final scene
input. Exact snapshots and scripts are retained under ignored
`character-light/scene-producers/area-update-witness.json` and
`area_update_witness.py`. General settings now come from the independently
executed original `45ff00 → 4ae410` activation witness; the auxiliary
postprocessing service remains zero-initialized. Re-running with those activated
settings preserves the listed effective stage values. The initial snap is now
source-proven: `43eea0` state `26e` completes
`433e20 → 446320` camera setup, gets the owner through `432220`, and at
`43f16e` calls `4829c0` to set `+44=1` before entering state `27a`. The next
`4829d0` snaps and clears that flag; later updates blend. Pass 185 and
`output-contract/area-startup-snap-witness.json` establish this writer and
execute it with the original update. This is still not a whole-game capture.

The reusable implementation prepares source scalar keys/flags/values once and
samples without allocation. `upper_bound` replaces the original linear search
while preserving last-key-at-or-before semantics, including duplicate times.
Version-specific absent lanes are zero. AREA selection prepares the inverse
XYZ transform and retains the inclusive bounding sphere, strict oriented-box
interior and first authored match. The game adapter owns STBL row meaning and
the environment channel-to-field mapping and supplies the recovered transition
weights. Engine `scene::Environment` owns the retained transition state; Stage
supplies world updates. There is no independent lighting clock or costume factor.

The recovered sky-white policy, character-field identity, renderable center
transform and world direction basis require no added adjustment. AREA-driven
ambient, field multipliers and output parameters must instead follow the now
verified live producer. A correction elsewhere in the
render/output pipeline must identify its own violated contract. If a genuinely
opaque boundary is replaced using Unreal, retain these verified input and
coordinate contracts and document the exact replacement boundary; do not
reinterpret this audit as permission for a costume or scene calibration factor.

Source exports inspected are retained under engine passes 15, 29, 36, 44, 50,
53, 66, 103, 136–139. Scoped compilation, pinned formatting, fatal clang-tidy
and focused view-light contracts verify this implementation. The final integrated
`python3 scripts/check.py all` passes; native PW5/PW0 first-visible and settled
captures show the correction. Physical acceptance remains open. No whole-scene
parity claim follows from this producer audit.

## Independent material input publication

A further bounded check executes original `547e70 → 547370` and its material
record uploaders on the actual retained PW5 model header. This supplies raw
MCTL records to the original semantic resolver; it does not feed our decoded
constants into a shader reference. Original `401800` initializes the modifier
table. External D3D calls are intercepted, and texture-resource slots contain
unique opaque handle tokens so the original resource index and shader-slot
selection can be observed without substituting image colors.

All nine CF1I material blocks publish constants and texture resource indices
matching the reconstructed decoded census, including the body records at
header offsets `0xd30` and `0x183c`. The comparison permits `1e-6` only for the
census's printed decimal values. Eight complete blocks execute through their
sampler-state records. The ninth, at `0x26d8`, publishes all compared constants
and textures before reaching an uninitialized global render-state service at
`46d7ea`; its subsequent state execution is explicitly unverified. Matrix-only
inherited commands, later live material overrides and actual GPU resource
contents are outside this witness.

Ignored scripts and results are `scene-producers/material_upload_witness.py`,
`material-upload-witness.json`, `compare_material_upload.py` and
`material-upload-comparison.json`. This narrows the material-input uncertainty
but does not turn initialized modifier tables into a proof of all later runtime
state. It supplies no new brightness adjustment.

The prepared `ScalarAnimation` and `AreaRegions` kernels were compared with
literal original `4ac280` and `482590` on actual `st04_c` data: all 186 channels
match for stepped/interpolated samples at 0, 0.5 and 1, and the three sampled
world points select identical regions. Separate original-instruction cases
confirm a rotated region, strict box-face exclusion and sphere rejection.
The public synthetic tests also cover duplicate keys, owned input lifetime,
first-region precedence and no query allocation. Scoped compilation, pinned
formatting and fatal clang-tidy passed for both kernels and their test unit.
Ignored comparison artifacts are `scalar-area-comparison.json` and its scripts.
The scene environment owner and live publication are now integrated. The
actual-data kernel checks establish their source input semantics; separate
native captures exercise complete current rendering and remain subject to
physical review.

## Authored pre-tone whole-scene RGB filtering

AREA publishes channels 14–16 as RGB and channel 147 as the filter mode.
The original output producer `461f00` queues mode 2 unconditionally, clamps
only the authored RGB factors to a minimum of zero, and invokes `56f880`.
This is a separate scene-image operation before luminance measurement and
bloom; replacing it with an exposure-key adjustment loses chromatic behavior
and the input to adaptation.

The recovered execution chain is `56f880 → 56f640 → 56f700 → 57a590`, with
state `56f6a0 → 570a10`. The executor binds its private shader pool vertex 0
and pixel 17 (not material-registry indices), publishes RGB plus alpha 1 at
pixel constant 32, and point-samples a clamped texture. Pixel bytecode performs
`out.rgb = sample.rgb * constant.rgb; out.a = sample.a` without saturation.
Depth tests/writes, alpha tests and blending are disabled. `463040 → 570900`
constructs the scene-size intermediate in D3D format 113 (RGBA16F).
Evidence is under ignored `character-light/output-contract/scaling-rgb-*` and
engine export passes 182–184.

The engine output packet now carries explicit global filter mode and color.
The macOS output owner retains a scene-size RGBA16F intermediate, executes
this pass before the log-luminance chain, and supplies the same resulting
image to final display. Mode zero bypasses the pass; other nonzero modes are
explicitly unsupported until their distinct source executors are implemented.
The pass runs even with tone mapping disabled. Live RGB, gamma and exposure
key changes do not reset exposure history or allocate replacement targets;
existing tone-enable transitions retain their explicit reset behavior.

Both C++ MCP corpora were queried for design and implementation. Full I.11 and
GPU.9 rules were read: the output owner retains accounted device images,
encoders remain borrowed, and target allocation occurs only on initial use
or extent changes. The native fixture exercises unsaturated RGB, unchanged
alpha, the minimum factor clamp, retained device storage, and a complete
filtered-image → luminance → display chain. It establishes these contracts,
not full-game visual parity or measured frame-time improvement.

Scoped verification after implementation: `memory-tests` built and passed with
headless native Metal; fatal clang-tidy passed for the output owner and native
fixture, pinned formatting was applied, and `git diff --check` passed. The
integrated full gate subsequently passed. Native capture inspection and physical
user acceptance remain distinct; the latter is still open.

## First native exposure measurement

Original `461f00` selects the bounded exposure-controller result whenever
history count is positive, including before the history ring fills.
`571200/5711b0` normalize optional lower/upper limits; `5712f0` applies them
only to returned output. The literal empty-history witness in
`output-contract/first-measurement-bounds-witness.json` demonstrates the cave's
`0.07..2.6` limits before any temporal sample is inserted.

The native asynchronous owner previously bypassed these limits on its first
submission because there was no completed readback. It now uses the actual
current GPU reduction with the common prepared output bounds: exponentiate,
floor at `0.001`, apply optional source-normalized limits, then supply its log
to both bloom and final transfer. CPU temporal state is untouched until a real
completed measurement arrives. No-history mode preserves the original raw
GPU branch. Bloom still divides by `exp(log + 0.001)` while final transfer
uses `exp(log) + 0.001`.

This is an explicit target first-sample lifecycle policy. The original initial
texture seed is not established, so the change claims exact bounds and
consumer equations, not original first-frame timing parity. No fabricated
luminance, warm-up sample, scene correction factor or synchronous extra
readback is introduced. Both design and implementation queried the two MCP
corpora and read I.1/GPU.1: normalized limits are an explicit common value
contract, and the first reduction stays device-resident. Native checks reset
the owner between cases and inspect the actual first submitted frame below
the minimum, above the maximum, and with history disabled.

The first-measurement correction passed both scoped `memory-tests` and
`resource-tests` after rebuilding. The lower-bound native case enables bloom
and chooses a threshold that would bloom from the unbounded sample, proving
both output consumers share the bound. Fatal clang-tidy passed for the common
controller, its contract tests, the native owner and the native fixture;
pinned formatting and `git diff --check` passed. The final integrated
`python3 scripts/check.py all` also passed: build, six CTest targets, fatal
clang-tidy, policy tests and repository content checks. Actual first-visible PW5
and PW0 captures no longer show the initial flash; physical acceptance remains
open.
