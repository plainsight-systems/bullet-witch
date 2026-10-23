# Stage shadow presentation

This is the source trace for slice 4's directional shadow pass. It records
confirmed behavior separately from the remaining implementation questions.
The active One Shot Kill scene enables this pass; retaining the earlier
unshadowed inspection policy does not complete its presentation.

## Evidence and method

Read the hash-bound unpacked executable with the existing Ghidra project and
`research/tools/engine_slice.py`. Pass 15's `00457620.c` supplies the pass
builder. Pass 51 (`pass-51-stage-shadow`) adds its camera, caster selection,
resource owner and queued command consumers. Generated exports and the scene
customization bytes remain under ignored `research/evidence/`.
Pass 55 (`pass-55-shadow-map-casters`) closes the static caster methods and
map attachment setup. Pass 52's adjacent `0x61b*` methods belong to a different
class and are not shadow-resource evidence.

The input is the `witch/stage/st04_c` scene record extracted by
`research/evidence/reconstruction/stage/customizer_probe.py`. Offsets below
are provenance within that record, not names for engine interfaces.

## Confirmed ownership and settings

The world pass builder (`0x45e130`) invokes the shadow builder (`0x457620`)
when renderer feature bit `0x20` is enabled. It supplies the shadow setup,
caster and restore queues before sky and ordinary scene submission.

| Scene offset | Actual value | Observed consumer |
| --- | --- | --- |
| `0xe8` | 0 | Shadow cull selector; maps to D3D cull mode 1 (none). |
| `0xec` | approximately 0.058 | Receiver parameter multiplied by 1/1024. |
| `0xf0` | raw 0; runtime 1 | Second queued field; the observed receiver executor does not read it. |
| `0xf4..0xfc` | (0, 1000, 0) | Directional light vector; adds 0.001 to Z before normalization. |
| `0x100` | 1000 | Extent used to build the camera frustum for light fitting. |
| `0x104` | approximately 0.15 | Caster screen-size selection threshold. |
| `0x108..0x10c` | (-1, 100) | Height planes for camera-frustum ray clipping. |
| `0x114..0x120` | runtime (60, 1, 8, 2) | Stage activation writes effective warp controls; raw zeros are not runtime settings. |
| `0x124..0x128` | (0.85, 100) | Camera/light alignment branch threshold and parallel-branch near extension. |
| `0x110` | 0 | Optional shadow-map preview command disabled. |
| `0x111` | 1 | Shadow camera and pass setup enabled. |
| `0x113` | 1 | Selected caster traversal enabled. |
| `0x12c` | 0 | D3D depth bias (render state 195). |
| `0x130` | 0 | D3D slope-scale depth bias (render state 175). |

The receiver's first parameter is not the raster depth bias. The builder
explicitly restores the two raster bias states to zero after the shadow
pass. It enables depth testing and writing with greater-or-equal comparison,
matching the scene's reversed depth convention.

Resource setup (`0x4630f0`) attaches a 1024 by 1024 D24S8 depth surface and
R32F color surface. Creation calls in the original disassembly request these
formats explicitly. Depth clears to zero. The queued receiver executor
(`0x457090`) broadcasts the first parameter to all four components of pixel
constant 38.

## Camera and caster selection

The builder constructs a finite light camera for caster selection, including
its 5,000-unit projection constant. This is distinct from the final raster and
receiver transform. The latter is the recovered camera-dependent perspective
warp: construct the shadow frustum, clip crossing rays to authored height
planes, select the camera/light-alignment branch, fit its perspective cone and
compose the homogeneous depth warp. The source light point is the normalized
light direction multiplied by 10,000. Caster and receiver commands publish the
same resulting matrix. [Projection baseline](shadow-projection-baseline.md)
records the exact runtime initializer, command chain and matrix witnesses.

Caster selection (`0x5e2f80`) iterates a registered owner list. It first calls
each owner's projected-size method, rejects results below the scene's 0.15
threshold, then calls the associated object's light-volume intersection
method. Accepted objects enter a temporary caster list. The builder calls
their virtual shadow submission method. This establishes a distinct caster
ownership path; it does not authorize drawing every visible mesh into the
shadow map without checking its original registration and material policy.

The static scene class constructed at `0x5e0eb0` registers in that list. Its
size method returns 1 and its intersection method returns 0; both pass the
selection predicates (the rejection sentinel is negative `FLT_MAX`). Its
shadow submission method skips a hidden owner, then submits the two original
model pass selectors. Background prop ownership was closed through the direct
BGOB construction path: `0x44a260` calls `0x455e60`/`0x5f0460`, applies authored
body matrices and `0x5f0080` group parameters, but does not call `0x4ea2a0`,
which enables the prop shadow-submission bit. Initial BGOB props therefore
receive shadows without being added as casters. Later runtime owner changes
remain outside the initial snapshot; residency alone never enables casting.

## Receiver shader

Original pixel program 13 declares a jitter volume at sampler 12, depth map
at sampler 13, world-to-light screen and texture matrices, and a shadow
parameter vector. Its loop takes two depth samples per iteration and averages
eight comparisons. The jitter coordinates depend on screen pixels at 1/32
scale. Samples outside the projected map rectangle resolve as lit. The
result multiplies directional diffuse and specular contributions rather than
darkening the entire completed material (which would also darken baked/SH
illumination incorrectly).

The jitter producer (`0x547a50`, pass 58) creates a 32 by 32 by 32, single-level
A8R8G8B8 volume. Its caller (`0x565590`) supplies dimensions 32 and sample-grid
width 8, binds it at sampler 12, and selects point minification/magnification
with wrapping U/V. Each texel packs two randomized disk samples from an 8 by
8 stratification. Polar conversion uses a square-root radius, sine/cosine,
and a per-screen-cell rotation. Components encode to bytes by truncating
`(component + 1) * 0.5 * 255`. Preserve the original BGRA memory ordering
when interpreting the shader's RGBA samples. The exact runtime random-stream
state is not recovered; this does not justify substituting a different filter.

Depth sampler 13 uses point minification, magnification and mip filtering.
The initialization instructions at `0x451cb9..0x451db0` pass raw address value
6 for U/V, 1 for W and zero border color. Value 6 is absent from Microsoft's
[documented D3D9 address enumeration](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtextureaddress).
The global device pointer is populated by the D3D device creation path, rather
than evidence of a custom sampler enumeration. The actual driver handling of
this unsupported state remains unresolved; do not relabel it as clamp or
border without evidence. The shader independently returns lit for receiver
coordinates outside the map rectangle.

The receiver matrix publication and missing runtime warp initialization are now
traced. The earlier reference-informed replacement and its correction are
recorded below. Initial registered prop caster eligibility is resolved by the
construction path above; runtime changes remain a distinct behavior owner.

## Runtime initialization correction

The slice-4 closure trace recovered the missing stage-node activation path:
`0x45ff80` calls `0x5f9c00`, which initializes runtime shadow fields at offsets
`0x114..0x120` to 60 degrees, 1, 8 and 2. It also sets the receiver secondary
field, caster threshold and setup flag. The earlier isolated zero-angle witness
used only extracted customization bytes and omitted this initializer. It is
therefore evidence about an uninitialized invocation, not the shipped-default
projection. The replacement now reconstructs the effective runtime path;
original machine-code witnesses validate its matrices at the initial pose,
two exterior checkpoints, translation, yaw and near-parallel alignment.
Detailed corrected call/layout evidence and implementation live in
[projection baseline](shadow-projection-baseline.md).

## Historical projection inference, superseded by initialization trace

The initial isolated machine-code witness passed extracted customization zeros to
`0x452460` and obtained NaNs; a 45-degree positive control returned a finite
matrix. That witness omitted the runtime initializer identified above and cannot
justify a replacement for the effective original projection. Its generated
inputs/results remain retained as historical evidence.

While initialization remained unresolved, the user authorized using pinned Unreal
source as a guide. The initial implementation adopted a single-map light-space
bounds fit from `ShadowSetup.cpp`, `SetupPerObjectProjection` and
`SetupWholeSceneProjection` at revision
`f45729c0b6f097038e72b52d366a62baeca3275b`. That provisional fit has been replaced
by the recovered original camera/light warp. The reference remains applicable
to explicit pass-resource ownership in `ShadowDepthRendering.cpp`,
`FProjectedShadowInfo::RenderDepth`, and to the cleared-border sampling policy.
It does not authorize retaining an inferred projection after the original
algorithm becomes recoverable.

The original jitter producer is initialized once into caller-owned storage;
its MSVC random stream receives an explicit seed because historical process
RNG state is unavailable. The output converts original BGRA memory bytes into
RGBA shader components. Non-finite polar samples in unused outer strata
encode as zero, matching the original x86 float-to-int sentinel's low byte;
the receiver's first four slices do not contain negative radii.

## Implemented portable boundary

`render/shadow.hpp` owns the fixed-storage camera/light warp, shared filter-guard
calculation and explicit-seed jitter producer. `game/stage_shadow.hpp` composes
the effective runtime initializer with the raw environment fields. The obsolete
global-AABB projection and geometry-bound accumulator have been removed.
Environment decoding retains the setup/caster flags, radius, height planes,
alignment controls and separate raster controls. The stage presentation owner
publishes the same projection into each selected caster/receiver draw. It owns
no shadow map; the native renderer owns retained attachments and jitter storage.

Portable verification compares six matrices against initialized original x86
machine-code witnesses and checks invalid controls, guard footprint, finite
outputs and the parallel branch. Runtime verification distinguishes decoded
fields from stage activation overrides. The existing jitter reproducibility and
independent sample check remain. These tests establish the numerical contracts;
actual stage and native edge observations are separate acceptance evidence.


## Native pass and verification

The Metal implementation retains accounted R32Float and Depth32Float map
attachments at the original 1024-square extent. Depth32 replaces the original
D24S8 attachment format on this target; no stencil operation is needed by this
pass. Static casters use the same position/palette transform as color and the
original raw-UV alpha coverage. The map clears to zero and tests greater-equal.
The color pass reads it after the caster encoder on the same command buffer.

The original jitter bytes reside in an accounted immutable buffer; explicit
wrapped point indexing reproduces the 32-cubed volume lookup. Eight comparisons
attenuate only directional diffuse/specular. Outside-map receivers remain lit.
A cleared guard region protects the complete filter footprint, with matching
projection inset and caster scissor. Individual point taps may clamp to the
texture edge, but that edge contains unoccluded zero depth rather than a repeated
caster. This deliberate portable policy follows pinned Unreal source because
the original address value is unsupported. [Filter baseline](shadow-filter-baseline.md)
records the revalidated device call, derivation and native boundary measurements. Map, jitter and retained draws release at the established completed-GPU
scene teardown boundary.

The existing native surface scenario passed actual Metal assertions for a
separate caster occluding directional energy while preserving baked lighting,
removing the caster restoring light, and an outside-map receiver staying lit.
Additional native checks pass all four edges/corners, exact crossing-tap averages,
reversed-depth equality and homogeneous caster/receiver W. The final device-budget
assertion confirms all owners release. This verifies
the pass consumer separately from the portable projection tests; actual stage
captures are recorded with the final scene acceptance evidence.

## Actual scene coverage and movement

The native offscreen probe uses the real mission-owned stage and authored props,
then rebuilds only the inspection view for explicit diagnostic camera poses.
The recovered projection passes initial and checkpoint-10 observations plus
right translations of .05, .1, .5 and 1 world unit, and yaw changes of 5 and
15 degrees. The separate checkpoint-20 view also passes with coherent nearby
wooden structures, ground and sky. The final view restores checkpoint 10. All submissions retain
4,100 ordinary draws, 674 prop draws and one sky draw. The authored exterior
containers, surrounding building, terrain and sky remain coherent.

The accompanying CPU observer applies the same published camera/shadow rows to
referenced vertices. It records no camera-visible references behind shadow W
for those poses. At the initial view, 1,290,358 of 1,347,345 camera-visible vertex
references fall inside the shadow volume; at checkpoint 10, 725,424 of 727,454 do.
Checkpoint 20 records 968,593 of 974,459 inside, with none behind shadow W.
These are repeated indexed-vertex references, not unique vertices or pixel
coverage. The original height-clipped/frustum-fitted volume does not cover every
visible surface; preserving it does not establish full shadow coverage or a
comparison against the running original game. Native edge tests separately
establish filter behavior where the footprint crosses the map boundary.

Evidence: ignored `shadow-closure-sweep.log`, `shadow-closure-sweep.png`, its
initial snapshots and `sweep0..6.png` captures. Source-derived camera dependence
is preserved: no texel snapping or stabilization optimization was introduced.

The actual Cocoa host also passed with the reconstructed warp and guard baseline.
`shadow-closure-host.png` shows the coherent exterior after 60 presented frames;
`shadow-closure-host.log` records 1,713 frames over the 60-second run and clean
exit status zero. The second exterior observation is retained in
`shadow-closure-checkpoint20.log` and PNG. These native observations complete the
slice's projection/filter acceptance alongside the source witnesses and boundary
checks; they do not establish full original-game image parity.
