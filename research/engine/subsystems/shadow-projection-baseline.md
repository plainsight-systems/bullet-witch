# Recovered stage shadow projection

The original camera-dependent perspective warp is now the implementation target.
The earlier global stage AABB projection is superseded. The old numerical witness
with zero warp angle is incomplete evidence: it omitted stage activation.

## Initialization and matrix identity

Stage selection `45ae00` resolves `witch/stage/<name>` through `41d480`, then
calls the stage observer's virtual slot +4. `45cbe0` constructs that observer
with vtable `82cdd4`; its slot +4 is `45ff80`. On first activation, that method
sets the node's initialized bit at payload -4, calls `5f9c00` on the payload,
then notifies observers. `5f9c00` writes +114=60 degrees, +118=1, +11c=8,
+120=2, +104=.15, +f0=1, +111=1 and +24=1. Raw customization bytes remain
raw data; these values belong to runtime stage initialization. Export pass66
contains the initializer, disassembly and reproducible command.

`452460` receives the actual camera as its second stack argument. Its output
is copied into both stack100 and stackc0 in `457620`. The command produced by
`44eb90` receives identity and stackc0 through `45fa60`; `460220` publishes
the same pair to the immediate rendering context. `44eb40` receives stack100,
whose consumer `457110`/`451fa0` publishes receiver constants. Thus caster and
receiver must share this warp. The separately constructed 5000-near-plane
light camera is used for caster selection and a temporary queued camera;
it does not justify using that fit as the final shadow receiver projection.

The initialized machine-code witness is finite. An independent analytical
prototype agrees within 1.2e-5 absolute matrix coefficient error at the initial
camera; the old NaN witness is retained only as an incomplete historical
experiment. Double arithmetic versus original single-precision intermediates
accounts for the small numerical difference. Additional poses and native
coverage are verified with implementation checks below.

## Algorithm and reference boundary

Build eight corners from the authored camera frustum, with the stage's shadow
far distance. Clip rays from the camera to the authored lower/upper height
planes when they cross those planes. Build a light point at the normalized
stage light direction times 10000 world units, including the original +.001
Z perturbation. Compare camera/light alignment against the authored threshold.
The ordinary branch reorients light axes around the camera forward direction,
uses the clipped far corners to locate its center, fits a perspective cone,
and applies the recovered homogeneous warp. The near-parallel branch uses
the source alternate light basis, near extension and far multiplier. Both
branches publish the same matrix to caster and receiver.

Pinned Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b` was inspected:
`DirectionalLightComponent.cpp::GetShadowSplitBoundsDepthRange` fits the camera
frustum to a sphere; `ShadowSetup.cpp::SetupWholeSceneProjection` applies border
scale and texel snapping. These were considered while the source initializer
was unknown. The recovered projection replaces that proposed fallback;
Unreal's sphere fit and snapping are **not** introduced into the faithful path.
Unreal's cleared border/scissor ownership remains the explicit native filtering
baseline: guard width derives from filter radius plus nearest-sample footprint,
not an aesthetic constant. Only homogeneous X/Y rows are inset for the guard;
source W and reverse-depth rows are preserved.

## Implemented boundary and verification

`StageEnvironment::parse` retains raw +100 shadow distance, +108/+10c height
limits, +124 alignment threshold and +128 near extension. The game-owned
`initial_stage_shadow_settings` composes runtime activation's 60-degree angle
and depth multiplier 2 without mutating resource data. The +118 offset cancels
algebraically in the final warp; +11c is not read by this matrix function.
Source activation enables setup independently of its raw stored bit; the
separate caster enable remains authored. Later scripted customization writes
are outside this initial-stage snapshot.

`render::make_directional_shadow_projection` uses the actual composed camera
pose/frustum and effective settings. Its rows are world-position dot products;
W is generally not 1. Both native passes use those same rows after world pose
and position scale. The former global-AABB fit and its geometry accumulator
have been removed from production. No shadow geometry walk or temporary
allocation occurs during projection.

The final caster matrix identity is independently grounded in command
consumers: `457130` installs command+20 through `45fac0`, copies command+60
into render context+10, then invokes `451e20`. That function composes the
context transforms and uploads vertex constants 8–11 at `451f95`. Command+60
contains `452460`'s warp, while command+20 is identity. Receiver `457110`
passes the duplicate warp to `451fa0` for pixel constants 30/34. The 5000-unit
light-camera fit therefore must not replace one side of this shared transform.

The derived guard is `ceil(radiusUV * 1024 + 0.5)` texels. The radius bounds
normalized RGBA jitter components; the half texel covers nearest sampling.
Admission rejects nonfinite/negative radii and a rounded guard at least half
the map. Active source radius .058 texel gives a one-texel cleared ring.
The projection multiplies X/Y by `(1024 - 2*guard)/1024`; depth and W remain
bit-identical when only the guard changes. The native owner clears the full
map and scissors casters to that interior. Larger test radii exercise all
edges and corners through the separate native filtering verification.

### Reproducible checks

Original-function observations use Unicorn 2.1.4 and pefile 2024.8.26 with the
locally unpacked executable. `research/evidence/reconstruction/stage/`
contains `shadow_matrix_initialized.py`, which invokes original `452460`
machine code with explicit camera data and the verified activation writes.
Its sqrt/tan hooks return the corresponding scalar math operations; this is a
bounded numerical witness, not a claim to have executed the entire game.
The earlier `shadow_matrix_emulator.py` omits activation and is superseded.

Run `python3 research/evidence/reconstruction/stage/shadow_matrix_initialized.py
<pose>` for each pose below; JSON records camera vectors, matrix and intercepted
math arguments. The six `shadow-matrix-native-<pose>.json` outputs remain
ignored local evidence. Native export provenance is
`research/evidence/engine/pass-66-shadow-initialization/command.json` and its
`functions/` and `instructions/` directories. The export command was
`python3 research/tools/engine_slice.py pass-66-shadow-initialization 0045ff80`;
its bounded dependency export also recovered `005f9c00`.

| Pose | Eye / camera change | Exercised behavior |
| --- | --- | --- |
| initial | (-61.3953, 1.35, -100.568), yaw -.28° | Active perspective warp |
| shift | Initial plus .1 unit along camera right | Translation-dependent fit |
| yaw | Initial eye, yaw 4.72° | Five-degree rotation |
| parallel | Initial eye, pitch 80° | Alternate alignment branch |
| checkpoint10 | (-7.98534, 1.35, 54.8025), yaw -.28° | Later mission camera |
| checkpoint20 | (76.5207, .8, 71.3654), yaw -61.25° | Different region/orientation |

Checkpoint eyes are the recorded diagnostic poses rounded to the shown
precision. Each witness uses those exact inputs on both implementations.
`tests/shadow_test.cpp` compares all 16 coefficients for every pose after the
shared one-texel guard conversion, within `2e-6 * max(1, abs(expected))`.
The independent compiled probe observed maximum scaled errors below 9e-7
for the first four poses; maximum absolute errors were 7.7e-6, 2.3e-5, 2.3e-5
and .0109 respectively (the alternate branch includes coefficients around
13000). This is numerical reconstruction, not bit-for-bit float execution.

`resource-tests` passed the six matrix witnesses, guard boundaries, invalid
inputs and original jitter samples. `runtime-tests` passed the independent
activation policy test. Logs: `slice4-shadow-warp-build.log` and the ignored
`warp-tidy-*.log` files. The portable implementation rejects invalid/singular
inputs before publication instead of allowing source NaNs to reach a GPU.
Native camera-sweep observations and stage coverage are reported in the main
stage shadow presentation research after the final capture run.
