# Character surface noise: material and vertex investigation

Status: investigation, 2026-10-25. No production changes in this investigation.
The outdoor PW5 and cave PW0 captures show conspicuous stippling/pinpricks on
skin, clothing and weapon surfaces. Appearance alone does not establish whether
these are shadow comparisons, failed depth replay, material highlights or texture
sampling. This report covers the material/vertex side; the renderer investigation
owns native shadow raster and receiver comparisons.

## Source position and preparation contract

The original shader registry maps every admitted effective material key to an S
preparation and T color program. Their clip position equations use the same
position expansion, source-count joint accumulation and local-screen matrix.
Preparation retains clip Z/W for its depth-valued color output. That color value
is separate from hardware raster depth; rounding its fragment divide is not by
itself evidence of an S/T hardware-depth mismatch.

An ignored instruction-interpreter witness evaluates all 24 distinct S/T vertex
pairs covering the 158 admitted material keys, with 128 input/palette cases per
pair. All 3,072 clip-position comparisons are bitwise equal. This establishes the
source equation correspondence under the interpreter's arithmetic, not the
original GPU's rounding or native Metal raster invariance.

Evidence: `research/evidence/reconstruction/lighting-noise-material/`
`source_vertex_correspondence.py` and `source-vertex-correspondence.json`.
The original instruction data remains under ignored lighting-source-fidelity
and shader-registry evidence. No raw decompilation is published here.

## Missing native position invariance declaration

At investigation time, `WorldColorVertex.position` in `world_color.metal` has
`[[position]]`, without `invariant`; `cmake/MacShaders.cmake` does not pass
`-fpreserve-invariance`. S and T use the same vertex entry point, but their
fragment consumers and pipeline compilation differ. Sharing a source helper is
not an explicit native compiler guarantee.

Apple requires both the position attribute and the compilation option when
multiple passes depend on identical positions. Its porting guide explicitly
uses depth preparation/replay as the example and warns about aggressive Apple
GPU optimization. This is a concrete missing platform contract, not proof that
it caused the captures. References:
[Apple silicon porting guide](https://developer.apple.com/documentation/apple-silicon/porting-your-metal-code-to-apple-silicon),
[Metal preserveInvariance](https://developer.apple.com/documentation/metal/mtlcompileoptions/preserveinvariance).

The pinned Unreal reference agrees. At revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Developer/Apple/MetalShaderFormat/Private/MetalShaderCompiler.cpp`
selects `-fpreserve-invariance` for supported vertex compilation; the adjacent
SPIR-V compilation path handles position invariance and FMA control separately.
Its no-fast-math/WPO policy is not a substitute for position invariance. These
reference files are retained only in ignored evidence. No Unreal code is copied
into production.

No shader adjustment was made on this evidence alone. A discriminating native
witness must establish whether S replay fails with the actual pipeline before
calling invariance the screenshot diagnosis.

## Native replay isolation

An ignored standalone consumer extends the production headless scene replay
fixture. It uses current production libraries/metallib, RGBA16F plus Depth32,
128-by-128 full-frame readback, the source integer-center viewport, and identical
draws with preparation disabled then enabled. Shadows are disabled; uniform
textures remove high-frequency texture sampling from this experiment.

The expansion covers admitted normal-map/occlusion programs `40800690`,
`80800690`, `c0800690` and `00820690`, variant 1: respectively three, two, one
and four joint influences. One hundred distinct palette/projection coefficient
sets per key all match exactly between S-off and S-on, 400 full-frame comparisons.
Every baseline must cover at least one pixel; empty images cannot pass silently.
Evidence: `replay.mm`, executable and `replay.log` in the same ignored directory.

A second 400-case run adds stage-scale joint translations and corresponding
camera cancellation, with source influence weights summing to one for each
count. It also matches all full frames exactly. Evidence:
`replay-stage-scale.mm` and `replay-stage-scale.log`. The reusable ignored
`compile-replay.sh` accepts a source, output and optional alternative metallib;
it does not change the production build.

These 800 comparisons do not reproduce depth-replay noise. They do not close the native
invariance risk, prove actual actor geometry correct, or test shadow self-reception.
The experiment does not justify a bias, tolerance, clamp or source-value change.

## Material and numerical competing causes

The existing original material census contains PW0 through PW5 and WG0 through
WG7: 14 models, 118 authored constant sets. None of the inspected constants is
nonfinite. Specular exponents span 0 through 450; Fresnel exponents are positive.
Diffuse material colors are all one; authored illumination RGB spans zero
through 0.5. This does not support a diagnosis of negative-power NaNs or corrupt
authored colors. It does not include arbitrary future script overrides or every
live actor draw. Summaries are in `material-ranges.json`.

The shader's normal decode consumes finite packed signed normals, and its
normal-map reconstruction follows the source absolute-value square root.
Zero-length normalization already has an explicit branch. Palette and material
publication validate finite values and required dependencies. No current actor
nonfinite input has been demonstrated. Do not add speculative clamps.

Large specular exponents and normal-map frequency can produce legitimate
aliasing and remain a competing visual cause. To discriminate them, the native
replay should hold geometry/light input fixed while separately disabling shadow
reception, preparation, and the material specular contribution, with pixel
readback of each outcome. Such switches belong to diagnostic consumers, not
new game behavior. Comparing screenshots alone cannot identify the first
violated boundary.

## Boundaries still open

- Actual actor geometry, poses, texture gradients and world/light transforms
  have not yet been replayed by this material-side witness.
- Shadow receiver/caster precision, depth sample coordinates and slope
  correction are being investigated independently by the renderer owner.
- Source equations and synthetic native pass matches do not constitute visual
  acceptance of the redesigned lighting. The reported noise remains unresolved
  until a complete native path reproduces it and a source/reference-supported
  change removes that failure.


## Actual-asset attribution, headless native replay

The ignored `actual-probe.mm` adapts the earlier character-shadow probe to current
mission resource ownership, const world-clock/mission-control observation,
authored character/enemy light fields, resident reflection slots and source
viewport policy. It constructs the real One Shot Kill stage, sky, placed props,
PW0 or PW5 and the initial weapon. It uses the current compiled production
renderer/metallib without launching a window. Initial and checkpoint-10 scripted
placements are explicit diagnostic inputs; it does not execute world startup or
claim a live gameplay capture. The actor remains at its initial standing pose.

Every capture stores the full 1024-by-576 RGBA16F target. PNGs are convenient
linear-HDR clamped previews, not production tone-mapped screenshots, and must
not be judged for absolute brightness. Separate marker captures identify visible
body and weapon pixels. Markers occupy a shader-unused constant component;
marked baseline output is bitwise identical to the unmarked baseline in all
four views. Instrumentation stays in ignored shader copies and does not widen
production resource or presentation APIs.

| Costume / placement | Body pixels | Weapon pixels | Body slope bound hits | Weapon slope bound hits |
| --- | ---: | ---: | ---: | ---: |
| PW5 initial cave | 22,488 | 15,614 | 71 | 24 |
| PW5 checkpoint 10 | 22,478 | 15,622 | 80 | 16 |
| PW0 initial cave | 28,103 | 15,577 | 85 | 24 |
| PW0 checkpoint 10 | 28,109 | 15,585 | 200 | 16 |

Counts above use raw slope magnitude greater than 0.05 after diagnostic half
readback; the separate saturated-mask measurement records 81 rather than 80 for
PW5 checkpoint 10 because threshold rounding differs at one pixel. No slope
fallback, nonfinite derivative, zero scale, zero determinant or half overflow
was observed in the visible actor masks. Most slopes are much smaller than the
bound; the largest isolated PW5 outdoor slope is about 31.28. A bound hit is not
by itself a visible defect.

All four actual views give these results:

- Disabling S preparation changes zero pixels, including both actor masks.
- Removing actor casters changes zero pixels. Removing world casters changes
  most body/weapon pixels; this includes legitimate world occlusion and cannot
  be counted as erroneous shadowing.
- Disabling actor reception through an ignored shader copy gives essentially
  the same actor result as removing all casters. This identifies reception as
  the path controlling broad actor darkness, not the cause of each bright point.
- Replacing the inferred receiver-plane correction with the original
  `numerator - sample * W >= 0` comparison and zero receiver correction changes
  zero masked body/weapon pixels in these four views. This does not establish
  original-device parity; it does rule out correction amplification as the
  observed difference for these particular samples.
- No nonfinite HDR component appears in the full production captures.

The production API correctly rejected the initial attempt to disable a draw's
shadow field while retaining its caster flag. That experiment was replaced by
an isolated diagnostic shader; validation was not weakened. Initial rejected
runs remain in ignored logs and are not counted as successful captures.

Reproduction artifacts and hashes: `actual-build-command.json`,
`actual-input-hashes.json`, `actual-attribution.json`, `actual-slope-source.json`,
`build-masks.py`, `build-slope.py`, `build-source-shadow.py` and captures named
`pw{0,5}-pose{0,3}-*`, all in the ignored lighting-noise-material evidence folder.
Here pose 0 is initial cave, pose 3 is checkpoint 10. These new actual-asset
observations supersede the earlier synthetic-only boundary for those two
standing-pose views, not for gameplay motion or the exact user camera poses.


## Front views, compiler isolation and actual pixel oracle

Additional checkpoint-10 cameras face the standing actor from the front at
normal height and at a lower upward angle. These are explicitly selected,
orthonormal diagnostic cameras, not a reconstruction of the user's exact view.
The actor is still beneath the doorway roof; "checkpoint 10" must not be read
as a proven sunlit actor position.

For normal-front and low-front PW5 views, removing receiver-plane correction
and using the original numerator comparison changes respectively one and 23
visible body pixels, and zero weapon pixels. S preparation disabled still gives
bitwise identical whole frames. Enabling the complete Metal invariance contract
in an ignored shader variant changes zero actor pixels in both front views and
in the PW0 cave view, and one body pixel in the back-facing checkpoint view.
It changes thousands of background pixels, so this is not a globally neutral
switch or evidence of a character-noise fix. Detailed counts:
`actual-front.json` and `actual-invariance.json`.

The cave weapon provides a useful material discriminator: 141 visible PW0
weapon pixels exceed 0.25 in at least one linear RGB component, while the
weapon shadow visibility is zero. Removing the reflection contribution removes
all 141 threshold crossings. These are diagnostic differences, not a proposed
removal of original reflection. Visibility captures keep production blending;
continuous values from layered PW5 surfaces are composited outputs, not a
histogram of individual eight-tap comparisons.

The ignored pixel-oracle capture preserves native float32 values by encoding
one value's bit pattern into four byte-valued half channels. It records final
interpolants, actual texture samples, material/environment constants, explicit
local-light count, camera-relative light positions and selected feature flags.
The decoder requires an exact record length, finite integral bytes within
0..255, a known draw owner, an explicitly opaque pipeline and Main opacity,
and a captured final shader output that rounds exactly to the production
RGBA16F pixel. Blended pixels and specialization/round-trip mismatches are
rejected rather than assigned a tolerance. This leaves seven valid PW0 cave
samples (three body, four weapon) and one PW5 low-front weapon sample from the
first candidate selection.

The renderer owner independently replayed the untouched original pixel shader
instructions with those actual captured inputs. All eight replay outputs round
to exactly the production half pixel; maximum float difference is approximately
6.86e-7. This includes the bright cave weapon sample with one local light and
zero directional visibility. The recovered material arithmetic reproduces that
highlight given those inputs. This is not proof that the bound texture/light
inputs or original-device sampling were identical to the original game, and
it does not yet cover a bright PW5 skin sample.

Artifacts: `oracle-probe.mm`, `oracle-capture.metal`,
`oracle-field-layout.json` (276 named fields), `decode-oracle.py`,
`pw0-pose0-oracle.json`, `pw5-pose9-oracle.json`, and the independently produced
`actual-oracle-replay.py`/JSON/log. Rejected candidate records and their exact
reason remain visible in the capture JSON. No production code or API was
modified for these experiments.


## Sunlit body sample and remaining input provenance

A further diagnostic places PW5 ten meters forward of the checkpoint-10
position and uses the same lower front-camera construction. This is an explicit
probe translation, not an authored checkpoint or gameplay movement claim. It
moves the actor beyond the doorway shade and produces direct-light highlights
on the shoulder/arm. Capture names use `pose12`; `actual-open-sky.json` records
its results.

S-off and the invariant compiler variant still change zero actor pixels. The
original numerator/no-plane shadow comparison changes 786 body and 19 weapon
pixels. Of the body changes, 785 become darker and one becomes brighter; all
19 weapon changes become darker. Only 19 changed body pixels reach the slope
bound, and none of the changed weapon pixels does. The source comparison
therefore mainly adds shadowing here; it has not been shown to selectively
remove the reported bright material stipples.

The broader lossless capture selects brightness, reflection-contribution
contrast, source-comparison contrast, spatial coverage and adjacent pixels.
Of 677 candidates, 69 satisfy all byte/opaque-owner/exact-final-output checks:
54 body and 15 weapon samples. This capture extends the layout to 284 fields,
including eight actual sampled shadow-map depths. Each older capture keeps its
own 276-field layout sidecar; the decoder uses that capture's layout and exact
length. Original PS replay of this new body set is owned by the renderer
investigation and is not claimed complete here.

One accepted body pixel at `(475,429)` illustrates the active path. Its black
environment sample, zero local-light count and full directional visibility
exclude cubemap and local light as this particular highlight's energy source.
The effective directional specular input is `(640,410,340,1)`, directional
diffuse is `(6.4,4.1,3.4,1)`, and the material exponent/scale is `(35,0.1)`.
Native specular RGB becomes `(64,41,34)` and final half RGB is approximately
`(31.42,20.30,17)`. These are captured inputs/results, not tuning targets.
Tracing the source publication of that directional packet is separate from
verifying the material equation with that packet.

A new source-coordinate distinction identified by the renderer investigation
also remains open: relevant original pixel programs reconstruct shadow
coordinates from interpolated world position in the pixel stage, while the
native shader currently interpolates vertex-computed shadow coordinates.
Captured `input.shadow_position` is native evidence and must not be presented
as proof of original coordinate construction fidelity. The owner is tracing
source matrix publication before a controlled native comparison.


## Source pixel-stage receiver-coordinate experiment

The renderer owner's isolated variants reconstruct translated shadow and
separately remapped texture matrices from the supplied camera-relative inputs,
then calculate receiver coordinates in the fragment stage. This diagnostic uses
shader-side reconstruction only to isolate the stage/order difference; a
production implementation must publish the source CPU constants. The owner
separately compared that constant construction with original CPU instructions.

On the sunlit PW5 pose, pixel-stage projection with the existing inferred
receiver correction changes 590 body pixels (321 brighter, 269 darker) and two
weapon pixels. Combining pixel-stage projection with original numerator
comparison and no inferred receiver correction changes 875 body pixels (90
brighter, 785 darker) and 19 weapon pixels. Both variants are bitwise identical
to the full cave PW0 frame. This demonstrates a real source-coordinate
departure affecting the sunlit native actor, while the cave weapon pinpricks
remain a separate material/input question. It does not establish that every
changed sunlit pixel is a corrected defect.

The ignored `pixel-projection*.metal` variants are owned by the renderer
investigation. This probe compiled them with the ordinary production flags,
retaining all other library modules, and ran the same production scene owners.
`actual-pixel-projection.json` contains the masked measurements. No production
shader, render API or game behavior was changed in this investigation.

## Actual bright-pixel material and local-light publication

The accepted sunlit PW5 body sample `(475,429)` belongs to draw 9, effective
material 15. The accepted cave weapon sample `(315,228)` belongs to **WA0**
draw 0, effective material 1. WA0 is the currently displayed ordinary weapon;
the earlier PW0–PW5/WG0–WG7 census does not by itself cover its material.
The new witness reads these exact installed resources, traverses their ordered
material continuations, then executes original modifier initialization
`401800`, global constant initialization `451460`, and material
resolution/upload `547e70/547370`. Only external D3D calls are intercepted;
the unrelated render-state executor is skipped. This is original-owned resource
input evidence, unlike replaying shader arithmetic with native supplied values.

| Input | PW5 body | WA0 weapon | Original publication |
| --- | --- | --- | --- |
| Effective key / variant | `00822690` / 1 | `c0802690` / 1 | Raw selector plus enabled fog/SH features; registry agrees |
| Surface programs | VS453 / PS123 | VS482 / PS47 | Original registry selection |
| Specular exponent / scale | 35 / 0.1 | 30 / 0.04 | SPCC → PS20, exact captured float bits |
| Reflection parameters | `(1,0,0,0.15)` | `(1,1,0,0.2)` | REFP → PS18, exact captured float bits |
| Reflection Fresnel | `(1,1,0,0)` | `(1,0,1,0)` | FRSR → PS6, exact captured float bits; only consumed by enabled programs |
| Environment power | `(1,1,1,1)` | `(1,1,1,1)` | ENVP → PS19, exact captured float bits |
| Material color | `(1,1,1,1)` | `(1,1,1,1)` | CCOL → PS3, exact captured float bits |
| Constant emission RGB | zero | zero | LUMI → PS16.xyz, exact captured float bits |
| Constant ambient | zero | zero | Global initialization → PS17, exact captured float bits |
| Indirect color A/B | one / one | one / one | Global initialization → PS0/PS1, exact captured float bits |
| Primary/secondary UV offset | zero / zero | zero / zero | Global initialization → VS4/VS5; no selected material override |

The captured `LUMI.w` contains the deliberate diagnostic draw identifier. It is
unused by these original programs and is explicitly excluded from the source
coefficient comparison; the original source uploads alpha one. UV offsets were
not fields in the lossless capture: their native equality is established by the
zero-initialized parameter type and absence of material overrides, rather than
claimed as another captured-field comparison. Shader-declared constants that
are disabled or unused are retained as such; they do not establish extra active
lighting contributions.

The cave weapon's local light was also traced independently of the captured
coefficient values. The installed lantern PHIT group selects joint 1 and
contains diffuse `(1.72,1.43,1.3)`, specular `(170,140,130)`, and attenuation
`(1.25,1,0)`. Original producer `5ed8b0` supplies alpha one to each color;
`5f65c0` initializes the permanent light's intensity/lifetime state to one.
`5f6e80` prepares the receiver packet with `546510` using zero additional
attenuation distance, then `5f6de0/42bb60` transfer it to the model owner.
The new execution witness runs original `546510` and `545f80` with these raw
resource colors, the previously traced placed-joint position, and the diagnostic
camera origin. Its PS44–PS47 position/attenuation/diffuse/specular packet matches
all 15 consumed captured components bit-for-bit. In particular the large
specular RGB is present in the original resource and survives the original CPU
upload unchanged. There is no evidence here for reducing either that RGB or
WA0's 0.04 specular scale.

This closes the investigated material coefficient/default publication and the
selected lantern's coefficient/position upload. It does not establish an
original live gameplay frame: dynamic material modifiers, full original
receiver-selection/lifetime execution, and later effect writers were not
replayed. The selected emitter and placed joint transform reuse the earlier
source placement/selection evidence. Directional AREA publication has its own
independent source witness; spatial irradiance, fog, pose, sampling, receiver
shadow policy and output remain separate boundaries in the main investigation.
No production material values were changed.

Reproduction and complete captured uploads are ignored evidence:

- `lighting-noise-material/material-input-publication.py`, `.json`, `.log`:
  both exact raw material chains, executable global defaults, program identity
  and captured coefficient comparisons.
- `lighting-noise-material/local-material-publication.py`, `.json`, `.log`:
  raw lantern resource identity and original camera-relative local-light upload.

## Production presentation of the frozen diagnostic scenes

A separate ignored `actual-presented-probe.mm` now uses the public
`WorldRenderer::prepare_presentation` and `encode_presentation` paths instead of
clamping HDR floats into an image. It retains the same resource owners, actor
pose, geometry, camera and production shader library as the latest raw receiver
captures. Stage output parameters are forwarded unchanged. The scene pass uses
the renderer's own HDR/depth attachments and source raster convention; the
final display encoder uses an owned 1024×576 BGRA8Unorm target and an exact-size
viewport at `(0,0)`. No window or actual game process is opened.

Every command is committed and completed before the next frame, preserving the
production previous-measurement and exposure-history ownership. Only a typed
presentation snapshot advances, at `1/60` second; the WorldClock and all actor,
material and stage state remain frozen. Frames 1, 119, 120, 179 and 180 are
retained. The run was extended beyond 120 because the authored 64-entry exposure
history samples at 30 Hz and needs more than 128 presentation frames to fill.
This is not a gameplay, motion, frame pacing or performance test.

Both scenes enable tone mapping (key 0.13, gamma 2.1), bloom (key 0.4, threshold
0.1, soft knee 12), global color mode 2, and a 64-entry history with slew 0.4 and
luminance bounds 0.07–2.6. The saved-setting edge filter remains off. Texture
mips retain the default disabled policy. In both scenes, the entire RGBA8 image
is identical at frames 1, 120, 179 and 180. This establishes stable output for
these frozen inputs, not exposure behavior under changing illumination.

The bright samples persist through actual output:

| Frozen scene/sample | Production raw HDR RGB | Presented RGB8, frames 1 and 180 |
| --- | --- | --- |
| Cave WA0 weapon `(315,228)` | `(0.5923,0.2495,0.1093)` | `(255,191,133)` |
| Cave WA0 weapon `(318,218)` | `(0.4158,0.1298,0.0726)` | `(240,141,110)` |
| Sunlit diagnostic PW5 body `(475,429)` | `(31.4219,20.2969,17)` | `(255,255,255)` |
| Sunlit diagnostic PW5 body `(511,351)` | `(1.9570,1.4600,1.3838)` | `(230,213,212)` |

Using the existing dominant-owner masks, the sunlit frame has 301 body pixels
with at least one saturated channel, including 229 white pixels, and 36 weapon
pixels with a saturated channel. The cave weapon has 22 such pixels, including
one white pixel; the cave body has none. These are display saturation counts,
not a classifier of defective highlights. The prior blended-mask limitations
still apply. The output captures connect the source-owned material responses to
the visible final pipeline; they neither demonstrate original-device visual
parity nor establish that every surviving highlight is correct.

Exact readback files are RGBA8 with only BGRA-to-RGBA channel reordering. PNGs
use the diagnostic bitmap's device-RGB metadata. Raw byte comparisons are
therefore authoritative for this target; these are not screenshots of the
macOS compositor, color profile or physical display. The sunlit camera remains
an explicitly translated diagnostic pose, not a matched original gameplay frame.

Ignored reproduction/evidence under `lighting-noise-material/`:

- `actual-presented-probe.mm`, `actual-presented-build-command.json` and build log;
- `presented-production-captures.json`: commands, forwarded parameter logs,
  source/executable/metallib/output hashes and scope;
- `pw0-pose0-presented-frame*.png` / `.rgba8.bin` and
  `pw5-pose12-presented-frame*.png` / `.rgba8.bin`;
- `presented-production-comparison.json`: complete-frame stability, mask counts
  and named raw-HDR/display samples.

## Material bandwidth, unit normals and pixel integration

The actual-content buffers now separate geometric normal, tangent, sampled
normal-map XY, reconstructed shading normal, UV derivatives, texture dimensions
and resident level count, directional specular, local specular, cube contribution,
visibility, raw interpolated basis lengths, pre-saturation half-vector response,
and draw/primitive identity. Diagnostic field images encode signed normals as
`0.5 N + 0.5` and label numerical scalar ranges. The float16 render-target
readbacks, rather than those visualization colors, are the numerical evidence.
Only exact opaque-owner pixels are admitted for arithmetic measurements; blended
surfaces retain production blending and are not treated as lossless scalar data.

PW5 body normals use a 512×1024 texture; the cave WA0 weapon uses 512×512. The
source-disabled mip policy publishes one level; enabled policy publishes eleven
and ten levels respectively. Their actual sampler requests anisotropy four,
anisotropic minification, linear magnification and linear mip filtering.
Singular values of the UV derivative footprint estimate major/minor texels per
pixel; they are **not** claimed as the hardware's exact selected anisotropic LOD.
The PW5 shoulder median footprint is roughly 2.11/1.35 in log2 major/minor texels.

The source algebra's independently normalized geometric normal and tangent do
not guarantee an orthogonal interpolated basis. At the previously captured PW5
body sample, their dot product is about 0.245 and the reconstructed normal length
is about 1.047. That can amplify the half-vector dot product before saturation.
The general final-normal normalization candidate removes that amplification
without changing authored light intensities, exponent, map values or basis
handedness. This is an explicit native adaptation, consistent with the pinned
Unreal tangent-to-world normalization identified in the independent review; it
is not presented as the original shader equation. It improves cave highlights,
but does not alone resolve the shoulder's high-frequency specular sampling.

The residual is present inside triangles. Neighbor measurements require exact
same opaque owner, draw and primitive, excluding silhouettes and mesh edges:

| Region | Quantity, neighboring p99 jump | Original, mips off | Unit normal, mips off | Unit normal, source mips on |
| --- | --- | ---: | ---: | ---: |
| PW5 shoulder, 2,586 pairs before mip coverage changes | Directional specular red | 46.47 | 44.25 | 32.31 |
| PW5 shoulder | Sampled normal-map XY distance | 0.556 | 0.556 | 0.241 |
| Cave WA0 weapon, 2,283 pairs | Local specular red | 0.588 | 0.501 | 0.490 |

These results distinguish a normal-length amplification from unresolved texture
bandwidth. Source mips reduce the latter substantially, but their setting remains
an explicit launch policy; this investigation does not silently enable them.

### Reference and bounded filter candidate

The numerical pixel-integration reference renders the same frozen scene at
4096×2304 and averages each 4×4 block in linear HDR before tone mapping or bloom.
The source integer-center convention needs a high-resolution viewport origin
of `(2,2)`, rather than `(0.5,0.5)`, to center those sixteen samples on the original
1024×576 pixel. Camera matrices, aspect and authored coefficients are unchanged.
The reference uses unit shading normals, default mips off, and full production
geometry and material evaluation. It is a finite sixteen-sample reference, not
an analytic integral or an original-device screenshot.

The candidate uses four equal-weight tensor Gauss samples at offsets
`±1/(2 sqrt(3))` in each pixel dimension. It interpolates the raw normal, tangent
and UV at each point, samples the normal map with half-size explicit gradients,
and reconstructs and normalizes each complete world-space shading normal.
The half gradients correspond to four equal-area samples; reusing the complete
pixel footprint at each point would filter the area twice. The mean and centered
variance of those four unit normals determine a Toksvig exponent/energy change:

- `v = mean(dot(n_i - mean(n), n_i - mean(n)))`, `L = sqrt(max(1 - v, 0))`;
- `p' = p L / (L + p (1 - L))`, amplitude `(p' + 1) / (p + 1)`;
- specular direction is the independently normalized mean, not the mean divided
  by a clamped variance length;
- constant samples give exactly zero centered variance; `p=0` and `L=0` have
  explicit limits, without an epsilon or a tuned roughness floor.

The [independent review](lighting-independent-review.md) records the pinned
Unreal implementation and primary NVIDIA Toksvig/normal-compression references.
This diagnostic applies the change only to directional/local power specular
lobes. It retains center light and half-vector evaluation, so four normal
samples approximate, rather than exactly integrate, the complete material. Diffuse, Fresnel and cube reflection retain the center unit normal. The
observed authored exponents are 35 for PW5 body and 30 for WA0; no negative
exponent is clamped or silently admitted. The engine implementation must define
that input contract and avoid extra sampling for materials without a relevant
specular consumer. No costume, level, material identifier or coefficient is used
as a special-case filter parameter.

The final comparison is stricter than the preliminary low-resolution mask:
every one of the sixteen reference subpixels must have exactly the same opaque
owner, draw, primitive and directional visibility as the low-resolution pixel.
All compared options use a common mask. Every low-resolution block containing
any debug-versus-production high-resolution mismatch is excluded as well (twelve
half components outdoors, maximum 0.0004883; two in the cave, maximum 0.0000153).
Neither exclusion changed the final admitted counts. This also removes the
known high-resolution VPOS shadow-jitter difference from the measured subset.

Temporal measurements translate only the camera by 1 mm along its right vector.
They retain common opaque identity, primitive and visibility across both poses,
and subtract the physical image change in a separately captured sixteen-sample
reference at each pose. They measure error relative to resolved motion rather
than counting all object motion as flicker. WorldClock, animation and material
state remain frozen. RMSE below uses the arithmetic RGB mean, not photometric
luminance.

| Region and accepted samples | Unit-normal baseline | Four-point candidate |
| --- | ---: | ---: |
| PW5 shoulder, 140 static pixels: HDR RGB mean absolute error | 0.283629 | 0.156362 |
| PW5 shoulder: mean-RGB RMSE | 0.913910 | 0.455791 |
| PW5 shoulder, 124 temporal pixels: motion-residual RGB mean absolute error | 0.328891 | 0.233583 |
| PW5 shoulder: motion-residual mean-RGB RMSE | 1.250300 | 1.049430 |
| Cave weapon, 744 static pixels: HDR RGB mean absolute error | 0.006160 | 0.003201 |
| Cave weapon: mean-RGB RMSE | 0.013067 | 0.005775 |
| Cave weapon, 585 temporal pixels: motion-residual RGB mean absolute error | 0.006146 | 0.002724 |
| Cave weapon: motion-residual mean-RGB RMSE | 0.013921 | 0.005218 |

The filter improves actual integration error and temporal residual in both
regions. This is stronger evidence than a lower brightness or saturation count.
The full opaque PW5 body subset also improves static and temporal errors. The
source-enabled mip path remains a stronger reduction of outdoor temporal error
than this candidate alone; the candidate is not a substitute for proper texture
minification or a claim that all aliasing is eliminated. No additional variants
or tuned constants were introduced after these measurements.

Actual presentation captures use the same 180-frame frozen-scene public output
path described above. Coherent highlights remain, cave grain reduces, and PW5
still has strong shoulder specularity. These images support the bounded
mechanism and help review its appearance; final user acceptance and production
integration verification remain separate.

Reproduction/evidence in ignored `lighting-noise-material/`:

- `baseline-production.metal`, `normalized-production.metal`, and frozen
  metallibs preserve the pre-adaptation and unit-normal comparisons;
- `build-stage-debug.py`, `actual-stage-debug.mm`, `stage-debug-*.json`,
  `primitive-debug-metrics.json`, and `*-diagnostic-fields.png` describe fields
  and same-primitive neighbor measurements;
- `actual-high-debug.mm`, `high-debug-manifest.json`, `*-high-*-debug*.hdr.bin`
  retain aligned high-resolution identity/visibility/linear HDR, including the
  shifted camera;
- `build-quadrature.py`, `quadrature-production.metal`, `quadrature-debug.metal`,
  `quadrature-manifest.json`, and `*-quadrature-presented-frame*.png` reproduce
  the bounded candidate;
- `analyze-strict-reference.py` and `strict-reference-temporal-metrics.json`
  contain the final common-mask rules, exact-size/finite guards, all admitted
  counts and full numeric comparisons. The earlier
  `reference-temporal-metrics.json` is preliminary and uses weaker masks.

### Integrated production capture verification

After the production helpers were integrated, the diagnostic raw and public
presentation probes were freshly linked against the current engine libraries.
The production metallib and source were frozen before the repository gate could
rebuild them. Both actual scenes were captured with that production library and
with the original frozen four-point candidate through the same newly linked
executables.

Production and candidate are **bit-identical across the complete frames**:
2,359,296 float16 components per raw HDR frame in each scene, and 2,359,296 RGBA8
components per presentation frame at frames 1, 119, 120, 179 and 180. The freshly
linked candidate also exactly reproduces the earlier diagnostic HDR and public
presentation captures. No numerical tolerance, owner mask or excluded pixels
were needed for this integration check. This establishes that the integrated
renderer carries the measured candidate through both material rendering and
production output for these actual scenes.

Ignored `run-final-production.py`, `compare-final-production.py`,
`final-production-captures.json` and `final-production-comparison.json` retain
commands, exact-size/finite checks, source/library/executable/output hashes and
the comparisons. The independent reviewer separately reproduced the strict
reference analysis and verified all 152 entries in `strict-evidence-manifest.json`.
The parent integration owns the required repository gate and user walkthrough;
this headless evidence does not substitute for either.
