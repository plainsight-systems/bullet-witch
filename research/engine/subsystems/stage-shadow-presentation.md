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

## Traversal floor-band investigation and caster correction

This records the uncorrected baseline. The later native footprint correction
below addresses the reproduced floor and character self-shadow artifacts.

The moving traversal view exposed fine floor bands that static captures had not
resolved. A bounded offscreen GPU probe now reproduces them with no actor and no
material animation. This isolates shadowing from actor movement, normal animation
and exposure adaptation; it does not establish the original game's appearance.
The diagnostic consumes the real retained `st04_c`/sky/background resources,
uses the production native renderer, and reads the direct RGBA16F color pass.
It saves display-clamped PNGs only for inspection. Tone mapping is bypassed for
measurement, so these PNGs are not final application output.

The source static shadow method `0x56b5a0 -> 0x56a4a0` submits selectors **0 and
1**. `0x421ff0` stores the selector at command +0x28; `0x427220` forwards it to
draw-context +0x84 with outer group zero at +0x80. `0x430310 -> 0x430b10` selects
the corresponding original chunk. The reconstruction previously marked all
4,100 static color draws as casters, including 571 chunk-3 draws. Admission now
retains only the 3,529 chunk-0/1 casters (3,053 opaque and 476 cutout); chunk-3
surfaces still render and receive shadows. This is a recovered selection rule,
not a floor-specific exclusion. The existing provisioned `startup_corpus`
scenario checks the new census and independently rejects any admitted caster
whose original retained chunk is outside 0/1.

The ownership trace does not justify excluding every `_background` model.
`0x4427a0` enumerates matching namespaces; `0x45bdb0` constructs ordinary models
through `0x5e0eb0`, which registers in the caster list unconditionally. The static
size/intersection methods pass caster selection. This differs from separately
placed BGOB props. No background-suffix exception was found in this bounded path.

### What the actual GPU isolation establishes

The exterior witness uses checkpoint-10 eye (-7.98534, 1.35, 54.8025), horizontal
yaw -0.28 degrees, plus separate +0.2-unit forward and +0.02-radian yaw poses.
The initial doorway pose was also tested. These are independent fixed captures,
not an assumption about which user input originally exposed the effect.

For the exterior image's lower-floor rectangle x=150..899, y=330..575:

| Diagnostic change | Mean shadow visibility |
| --- | ---: |
| Original reconstructed all-static admission | 0.504 |
| Recovered chunk-0/1 admission | 0.667 |
| Only three near-horizontal floor draws cast | 0.508 |
| Those three floor draws excluded diagnostically | 0.997 |

The three isolated draws are `st04_c` draw 740, chunk 3, at Y=.01, and
`st04_c_background` draws 168/169, chunk 0, at Y approximately .000004.
The chunk-3 correction removes the first from casting; the source-admitted
background plane still produces visible self-shadow bands. The floor-only and
no-floor alternatives are **diagnostics only**, not production policies.
Disabling only receiver shadowing removes the fine bands while retaining normal
mapping, textures and fixed lighting. An independent analytic nearest-depth
plane calculation using the recovered matrix also produces alternating lit/dark
samples with zero comparison bias; that calculation is a mechanism witness,
not original-game or GPU execution.

The original receiver compares `receiverNumerator - sampledDepth * W >= 0`.
The reconstruction divides first. Testing the original expression in an ignored
shader variant changed 1,822 of 589,824 visibility pixels at the exterior pose;
it did not remove the bands. At the nearby forward/yaw poses the changed counts
were 2,757 and 2,539. This rules out that algebraic precision difference as the
main cure for this reproduction. Caster and receiver use the same live matrix;
no stale-view mismatch was demonstrated.

### Source settings checked; remaining uncertainty

Original caster pixel programs 0/1 compute depth plus
`c68.x + c68.y * max(abs(ddx(depth)), abs(ddy(depth)))`. The native caster currently
omits these shader bias terms. `0x457620` publishes scene +0x12c/+0x130 to c68
and also to D3D depth/slope-bias states. The actual retained scene bytes contain
zero for both, and `0x5f9c00` does not change them. Therefore this unsupported
nonzero behavior is not an established cause for the current scene. A search of
recovered mission/script listings found no explicit shadow/depth-bias writes;
that negative search does not prove absence of every later native customization.

The original shadow cull selector at scene +0xe8 is zero. `0x457620` maps it to
D3DCULL_NONE (1); the native no-culling mode agrees. `0x451fa0` uses the exact
texture remap (.5, -.5, .5, .5), with no additional half-texel constant. Original
D3D9 raster/sample conventions and the complete later runtime policy remain
separate investigation boundaries. Neither texel snapping, an arbitrary bias,
nor dropping all floor casters has been introduced as a purported source fix.
The demonstrated admission error is corrected; the remaining floor self-shadow
artifact is **unresolved**, not accepted as final visual parity.

### Reproduction and files

Ignored evidence lives in `research/evidence/reconstruction/shadow-motion/`:
`probe.mm`, `build-command.json`, shader snapshots/variants, per-run logs,
RGBA16F `.hdr.bin` captures, PNGs and `metrics.json`. The executable links the
current application libraries; the command JSON preserves the exact build.
Run from the repository root, supplying the readonly game root:

```sh
research/evidence/reconstruction/shadow-motion/probe "$GAME_ROOT" \
  research/evidence/reconstruction/shadow-motion/visibility.metal \
  research/evidence/reconstruction/shadow-motion/visibility-3.png 3
```

Pose IDs 0/1/2 are initial/forward/yaw; 3/4/5 are exterior/forward/yaw.
`baseline.metal` is the production shader snapshot. `unshadowed.metal` returns
visibility one; `source-compare.metal` changes only the comparison expression;
`visibility.metal` and `visibility-source.metal` publish the corresponding
visibility directly. `PROBE_CAST_MODE=only01`, `floor-only` or `no-floor` controls
only diagnostic caster admission through a forwarding output adapter. Baseline
captures preceded the production chunk correction and are retained rather than
silently overwritten. The analytic script/results are adjacent under
`research/evidence/reconstruction/stage/self-shadow-plane-probe.*`.

The narrow production correction queried and read both MCP corpora in design
and implementation: I.1 makes admission explicit, GPU.1 keeps it a setup-time
retained decision without production readback. Pinned formatting and fatal tidy
cover the changed translation units; the integration owner runs the coherent
full gate and original-content corpus after parallel edits finish.


## Native receiver-footprint correction

User review found remaining character shadow artifacts after the caster-selector
correction. A separate costume-5 probe uses the original checkpoint-10 actor
placement, original stage resources, retained skin palettes and the native GPU
renderer. Both character and weapon **do cast**: they have no placed-prop record,
so the current admission predicate selects their opaque/cutout chunks too. The
static-only diagnostic above did not establish their behavior.

The character probe compares the same pose with all casters, only actor/weapon
casters, and actor/weapon casters removed. Across 20,904 body pixels at this
exterior view, baseline average visibility is 0.26131. Removing only actor/weapon
casters makes all body pixels lit; the actor-only map reproduces the baseline
body visibility. This demonstrates an actor self-shadow contribution. It does
not imply all of that contribution is erroneous: clothing, limbs and the weapon
must retain their real mutual occlusion. Source vertex programs 453 and 3 confirm
the same four-influence accumulation and homogeneous position path for color and
shadow geometry. No separate stale skin-palette or double-world transform was
found.

Two exact-source arithmetic probes are insufficient: changing the receiver to
`numerator - sampledDepth * W` changes only 22 body pixels, and publishing the
original interpolated caster `z/w` instead of raster depth changes only nine.
Neither removes the reproduced pattern. A separate D3D9 integer-pixel-center
conversion also does not cure it; it reduces average body visibility to 0.17622.
That experiment remains diagnostic, not a production compensation. Microsoft's
[Direct3D coordinate-system documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-coordinates)
and [texel-to-pixel mapping](https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels)
establish the API convention difference, not an original-game fix.

### Explicit inference from a mature implementation

The original zero-bias configuration does not resolve self-shadowing after the
bounded source trace. Following the authorized reconstruction policy, inspect
Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`:

- `Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapProjectionDirectional.ush`,
  `ComputeDepthSlopeDirectionalUV`, `ComputeOptimalSlopeBiasDirectional`, and
  the ray-start correction: derive a geometric depth slope, bound it to
  ±0.05 in normalized shadow coordinates, and apply
  `2 * max(0, dot(depthSlopeUV, sampleOffsetUV))` in reversed depth.
- `Engine/Shaders/Private/ShadowFilteringCommon.ush`, `Manual3x3PCF` and related
  samplers: identify the actual texel centers and retain the sampling footprint.
- `Engine/Shaders/Private/ShadowDepthVertexShader.usf`,
  `ComputeDepthBiasDirectionalSpot`: separate constant and slope contributions.
  Its configurable caster bias is not adopted here.

The native adaptation derives the receiver triangle's depth plane from screen
space derivatives of projected shadow UV/depth, because this game's recovered
light projection is perspective warped. It evaluates derivatives before alpha
cutout discard or divergent shadow rejection. A common derivative scale avoids
cross-product overflow/underflow; nonfinite derivatives or a zero Jacobian produce zero slope. A zero homogeneous W is
unshadowed. The same bounded Unreal slope rule
handles near-degenerate projections. Each of the original eight jitter taps uses
its actual nearest texel center, including clamp-to-edge address resolution,
to calculate the conservative positive-depth
correction. A flat plane receives no correction. The correction scales with
sample displacement and map resolution; no scene-specific constant depth offset
or normal displacement is introduced.

This is an **inferred native algorithm**, not recovered Bullet Witch shader
parity. It retains the source projection, 1024-square map, original jitter bytes,
eight comparisons, filter radius, material lighting equations, caster admission
and cleared border. It adds arithmetic and fragment derivatives without adding
texture samples, buffers, allocations or synchronization. No performance claim
is based on these diagnostic captures. Core I.1 and performance GPU.10 were
queried and read in both phases: explicit projection/sample contracts and
completed GPU correctness measurements, not CPU submission-time claims.

### Verification and limits

The existing native surface fixture now checks a sloped plane at three subtexel
phases, a separate thin occluder and adjacent lit contact edge, a singular UV
projection, and a steep slope whose result stays within the lighting endpoints.
Near-zero uniform homogeneous scale and zero W are also exercised. The old
shader fails the new tilted-plane test, while the corrected shader passes.
Existing reverse-depth equality, homogeneous W, original eight-tap averages,
cleared-border and outside-map contracts remain in the same fixture. The final
scoped check passed pinned formatting, fatal clang-tidy, Metal compilation with
`-Wall -Wextra -Werror`, and the native surface fixture, including a thin
occluder only 0.0000475 normalized depth ahead of its receiver at the measured
contact sample. This separation exceeds the local derived texel correction.
The coherent application build and live review remain the parent integration gate.

For the real checkpoint-10 scene, the sampled lower-floor region's average
visibility changes from 0.69748 to 0.99877. Body visibility changes from 0.26131
to 0.33400 and weapon visibility from 0.40088 to 0.69119: occlusion remains,
rather than making the actor globally unshadowed. With the camera translated
0.2 along Z, floor visibility is 0.99931; with yaw advanced 0.02 radians it is
0.99977. The direct HDR results are finite. Fine horizontal floor bands are
visibly removed in these captures; the larger diagonal floor pattern remains
authored surface detail. These fixed-pose checks do not establish all-animation,
all-view original image parity or replace subsequent user review of the native
application. Conservative slope correction can still trade very close-contact
precision for stability, particularly at the bounded steep-slope limit.

Evidence is organized under `research/evidence/reconstruction/character-shadow/`:
`probe.mm`, `build-command.json`, baseline/caster-isolation images,
`production-{0,3,4,5}.png`, visibility/mask images, `production-metrics.json`, and
`surface-contract-production.log`. The probe leaves game files read-only and
opens no window. The original-source and Unreal excerpts are ignored evidence;
only this reviewed account and authored code/tests are tracked.
