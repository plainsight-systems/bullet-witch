# Rejected stage image: display fidelity audit

October 23, 2026. This is a diagnosis of the rejected stage image, not visual
acceptance. The current inspection image has a recognizable cave corridor but a
clipped exterior opening. No brightness or material adjustments are proposed. The follow-up source-backed
state/sampling corrections below were authorized separately after the research.

## What the source does establish

The selected ordinary material program explicitly adds directional diffuse to
interpolated baked vertex illumination. The program's pixel registers 23/24
hold diffuse/specular color and register 25 holds direction. The scene light
builder passes authored diffuse/specular into the constructor with its distance
argument zero. That selects the constructor's direct-copy branch; it does not
multiply those colors by an unobserved attenuation coefficient. The register
uploader transfers six consecutive vectors beginning at register 21. Thus adding
those two contributions is consistent with that selected program. It does not
prove that selecting an unshadowed program reproduces the original stage pass.

Evidence: scene builder `0x460660`, queue builder `0x422170`, light constructor
`0x546510` (especially `0x546688`–`0x546695`), uploader `0x545ea0`, and recovered
pixel program 221. Raw witnesses and program disassembly are under
`research/evidence/reconstruction/world-output/` and `world-renderer/`.

The final output program exposes scene RGB with the authored key, divides by
exponential log luminance plus 0.001, applies reciprocal authored gamma, then
adds bloom. It has no compressive filmic curve. Consequently clipping in an
8-bit target is possible even when that individual expression is translated
correctly. Arbitrarily adding a modern tone curve would change the source.

The luminance target dimensions and destination-normalized reduction offsets
are confirmed independently in the target allocator, command builder, offset
helper and shader. In particular the last reduction uses a 1×1 destination and
samples clamped corners of its 4×4 source. That unusual footprint is present in
the recovered source; changing it to a conventional box average is not justified
by appearance alone. Its complete result still depends on the preceding raster
sample centers, described below.

## A concrete sample-center correspondence gap

The native fullscreen helper `0x585d90` places its left/top clip coordinates at
`-1 - 0.5 / width` and `1 + 0.5 / height`, forwarding UV corners 0 and 1. The
fullscreen vertex program passes these coordinates through. Raw constants at
`0x6c1398`, `0x6c139c` and `0x6c1da4` are 0.5, 1 and -1 respectively; the half
constant must not be mistaken for one.

Direct3D 9 places pixel centers at integer screen coordinates. Under that
viewport convention this quad begins at screen coordinate -0.25 and the first
pixel's interpolated UV is `0.25 / destination_extent`. The prior Metal quad
had no clip displacement, giving `0.5 / destination_extent`. This is a real
sampling difference, not an exposure-setting difference. Microsoft documents
the API convention in [Directly Mapping Texels to Pixels](https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels).

The final 4→1 reduction chooses the same clamped corners for either convention,
but earlier reductions and the initial bilinear luminance image can differ.
Numerical impact on the actual scene must be measured before attributing the
white exterior to this discrepancy. The source-backed correction is described
below; it is not presented as the cause of that clipping.

## Remaining attribution work

- Capture raw HDR color and identify actual energy at clipped exterior pixels,
  then independently replay the implemented reduction/bloom/output equations.
  Display PNG values alone cannot distinguish excessive input energy, incorrect
  exposure measurement and bloom contribution.
- Compare the original fullscreen sampling convention using the same captured
  HDR image. Keep this comparison separate from changing camera geometry.
- Establish the original initial camera and projection. Current inspection
  framing is not evidence of original visibility or exposure framing. Other
  agents own this source trace.
- Separate original temporal adaptation initialization from the current first
  submission policy, which uses current GPU luminance. The authored parameters
  are recovered; the actual game clock and first-frame schedule are not.
- Preserve the distinction between exact selected material expressions and the
  full original shadow/light/visibility pass selection. An unshadowed selected
  program cannot establish stage lighting parity.

## Source-backed correction contract

The actual main world setup queues depth comparison GREATER_EQUAL, enables
writing for the opaque phases, and disables writing for the alpha phase
(`0x460920`). This agrees with the recovered near-one/far-zero projection;
color vertex programs pass its clip depth through unchanged. The actual target initializer `0x462cc0` iterates the three main target variants
at owner + 0x20 + index × 0x100. At `0x462ebf` it writes depth clear zero to
target + 0x88; at `0x462ed3` it enables depth clearing at +0xf4. The live target
selector `0x463800` returns those same records. Executor `0x4079b0` supplies
the +0x88 value to Direct3D Clear. This closes the source chain from selection
to assignment to consumption; native presentation now clears zero and rejects
incompatible forward-depth submissions. Caller-owned isolated color fixtures
keep their own explicit depth policy.

The main pass executor `0x564f70` additionally changes minification for surface
samplers 0–3 to anisotropic and sets maximum anisotropy four. Those commands
follow the global wrap/linear sampler defaults. Sampler 7 is not overridden,
so environment sampling retains clamp/linear with anisotropy one. The previous
inspection sampler omitted this main-pass override.

The common surface contract now represents greater-depth comparisons and
anisotropic minification explicitly, with validated range 1–16 and a default
of one for non-anisotropic filters. Native sampler/depth objects are still
created at submission setup. No intermediate readbacks or hot-loop allocation
are introduced. Unreal Engine's pinned MetalState.cpp FindOrCreateSamplerState
and GetMetalMaxAnisotropy demonstrate the same platform translation: linear
min/mag filtering with a separate maximum-anisotropy parameter. We follow that
API boundary, while selecting four from Bullet Witch source rather than an
Unreal default. Pinned revision remains
`f45729c0b6f097038e72b52d366a62baeca3275b`.

For fullscreen sampling, the native device's getDefaultSamplePositions result
for one sample is (0.5,0.5), recorded independently in ignored evidence. Moving
the Metal fullscreen quad right/down by 0.25 destination pixels yields the
original UV `(pixel + 0.25) / extent`. This deliberately differs in sign from
copying the original D3D9 clip displacement. A nonuniform bilinear-luminance
fixture checks the resulting first sample against a specific source texel;
the earlier corner-reduction test alone could not distinguish these cases.

C++ design retrieved Core Guidelines I.5 (explicit preconditions) and performance
GPU.1 (retained GPU intermediates). Implementation separately retrieved E.4
(transactional state invariants) and GPU.1. Invalid anisotropy must leave the
previous submission usable. Device tests exercise that invariant and two
overlapping reverse-depth surfaces in adverse submission order.


## Correction verification

The native memory-test executable passes after these corrections. Its device
cases check overlapping reverse-depth surfaces through the production HDR
presentation path, invalid anisotropy retaining the previous submission, and
a nonuniform bilinear luminance sample whose expected value changes under the
old sample-center mapping. The existing exposure/gamma and resource retirement
cases also pass. Targeted clang-tidy reports no diagnostics for the renderer,
postprocess owner, startup host and native test source.

The host now uses identical integer dimensions for its HDR target and final
world viewport, preserving the sampling contract during aspect fitting. The
integrated startup build and corrected-camera capture remain separate checks;
these device results do not establish original full-scene visual parity or
user acceptance. Build, test and lint evidence is retained under
`research/evidence/reconstruction/world-output/`.

## Corrected-camera HDR attribution

The corrected-camera offscreen capture retains its raw 1024 × 576 RGBA16Float
scene as well as the final PNG. An independent NumPy replay applies the traced
sampling footprints and equations, rounding each half-float intermediate at its
native target boundary. It reproduces the production PNG within one byte per
channel; mean absolute difference is 0.0617 byte. This is a numerical comparison
of this captured first frame, not an original-game image comparison.

The recovered reduction produces log luminance -6.217097, geometric luminance
0.00199503 and a final linear exposure multiplier of 60.0996. Raw scene RGB has
no nonfinite values and a maximum of 0.432861. Exposure and gamma alone clip
48,719 color channels; adding the recovered bloom raises that count to 75,423.
Bloom reaches 2.18555 before display clamping. Thus the bright exterior is
reproduced by the current input and output equations; it is not an unexplained
Metal display-format conversion.

Replaying the prior unshifted sample-center convention against the **same** raw
HDR yields exposure 59.9934, only about 0.18 percent different. That confirms the
sample-center correction is necessary for correspondence but does not explain
the exterior brightness. No authored light or display values were changed.
The raw input still reflects inspection material/pass selection, and the original
first-frame adaptation schedule remains separate from the current first-frame
policy. Those boundaries prevent this comparison from establishing original
stage lighting parity.

Ignored artifacts: `stage/offscreen-stage-original-camera.png.hdr.bin`,
`world-output/replay_display.py`, `world-output/replay-original-camera.png`
and its JSON report, plus the same-input `replay-prior-sampling` comparison,
all beneath `research/evidence/reconstruction/`.

## Bounded source association recheck

The bright exterior prompted a second check of the original builder and resource
constructor rather than a change to the curve. Constructor `0x56e990`, builder
`0x56d6a0` and their executors establish this association:

| Pass | Input texture field in original owner | Destination and format | Sampling |
|---|---|---|---|
| Scene logarithm/color | Builder's scene argument | 512² RGBA16Float (`+0xd00`) | Clamp, linear; executor `0x579620` |
| Color/luminance split | `+0x28`, the 512² texture | 256² RGBA16Float and R32Float (`+0xd04/+0xd08`) | Clamp, point, four offsets; executor `0x579770` |
| 256→64 | `+0x13c`, 256² luminance | 64² R32Float (`+0xd0c`) | Clamp, point, sixteen offsets |
| 64→16 | `+0x228`, 64² luminance | 16² R32Float (`+0xd10`) | Same |
| 16→4 | `+0x328`, 16² luminance | 4² R32Float (`+0xd14`) | Same |
| 4→1 | `+0x428`, 4² luminance | 1² R32Float (`+0xd28`) | Same |

All four reduction commands use executor `0x578950`, binding command `+0x14`
and deriving offsets with `0x5788b0` from the builder's destination width/height.
The native target chain agrees with these associations. This recheck found no
additional dimension, input-texture or point/linear mismatch.

The captured raw RGB distribution also explains why its geometric luminance can
be small despite a brighter opening: median 0.001639, 75th percentile 0.003027,
and 99th percentile 0.190063. A maximum alone is not a measure of geometric mean.
That observation does not certify the material/pass inputs against the original.
The remaining narrow output source question is the initial CPU user-luminance
value and history continuity when the original stage first submits tone mapping.
It remains unresolved here; no initialization constant or brightness correction
is inferred from the screenshot.

## Live-host configuration defect

A subsequent integration audit found a concrete discrepancy outside the shader
chain: `StartupHost` inherited an empty default `set_world_output_parameters`
implementation. `StageInspection` called that interface on the host, which
silently discarded the authored tone, bloom and adaptation settings. The
headless probe called `WorldRenderer` directly and therefore did not exercise
this failure. A correct offscreen image was not evidence of correct live-host
configuration; this explains why earlier analysis of those captures could not
account for the user's much darker window.

The common setting operation is now pure virtual. `StartupHost` explicitly
forwards it on the main thread to the same retained renderer used by `draw()`.
No extra resource owner, image adjustment or curve is introduced. The corpus
fixture explicitly records the settings and checks authored policy publication;
that fixture is not claimed as native host validation. The ignored
`host_stage_probe.mm` instead loads the same stage through actual `StartupHost`,
publishes `StageInspection` through that adapter and presents several frames,
allowing a real window capture without repeating menu navigation.

Design consulted C.121 for a required pure interface and GPU.9 for retained
postprocess ownership. Implementation separately consulted C.128 for explicit
overrides and GPU.9 again. This is a missing delegation correction within the
established host/device split, not a new rendering algorithm. Native-window
acceptance is recorded only after executing that actual host path.

## Follow-up scheduling and enabled-effect checks

The original stage composition calls CPU adaptation before queuing this frame's
tone-mapping commands. It therefore reads the preceding completed luminance
surface. The static constructor zeroes history, index, full flag and adapted
value; changing history length resets those values, while reapplying the same
length preserves them. Before the history fills, each update immediately uses
the measured luminance. Thus a first-frame initialization difference cannot by
itself explain a persistent bright opening in a stationary view. The native
owner follows previous-completed-sample adaptation after its first submission;
its deliberate first-submission/current-GPU policy and later stage-to-stage
history continuity remain distinct integration boundaries.

The global post-effect settings come from `witch/general/setup`, resolved by
the original scene constructor. The actual publication disables the pre-tone
flags at +0x120/+0x121 and final global correction at +0x134. The selected stage
also disables its optional effects at +0x168/+0x178/+0x18c. These checks found no
missing enabled color-adjustment pass. Raw scheduling instructions and enabled
flag census are retained under `world-output/adaptation-schedule-witness.txt`
and `world-output/post-effect-enable-census.json`. They support eliminating
these specific hypotheses, not complete pass-graph parity.

The actual-host diagnostic subsequently ran through `StartupHost` with the
forwarding fix and source-derived initial view. After sixty presented frames,
a native window capture showed textured cave walls/floor, the metal entrance
and the exterior building. The earlier nearly black live-host result is no
longer reproduced. The bright exterior floor remains visibly clipped and is
not declared fixed by the adapter correction. Evidence:
`stage/host-stage-initial.log` and `stage/host-stage-initial.png` beneath the
ignored reconstruction evidence directory. The diagnostic closes itself after
a bounded sixty-second run and does not alter the normal mission launch path.

### Runtime irradiance selection: resolved black sky

The missing input was the runtime spherical-harmonic feature bit, not a
material ambient override. Native selector `42f970` adds `0x200` when feature
byte `1adfd` is enabled; constructor `42fb50` enables it. The earlier inspection
mask omitted that bit and selected sky pixel program 362, which has no SH
inputs. The correct effective sky selector `06e00200100` selects vertex 448 /
pixel 363. The earlier interpretation that the sky's raw selector disabled SH
was incorrect: shadow/local-light feature bits are independent of this switch.

Sky constructor `5e3660` supplies white raw SH through `545610` and `42bbc0`.
Command `4223b0` captures nine raw vectors; executor `4277b0` packs them with
`5453a0` and uploads seven vectors to pixel registers 9–15. The packed white
field has first three vectors `(0,0,0,0.9999998807907104)`, next three zero,
and last `(0,0,0,1)`. The constant is the original float product
`3.544907569885254 * 0.2820947766304016`. Pixel 363 evaluates the linear and
quadratic polynomial at the normalized geometric normal, then adds material
ambient and illumination before multiplying the base texture. Material ambient
remains zero; no substitute light value is injected into that register.

Ordinary objects select the same runtime feature. Their virtual initialization
`5e46d0` constructs uniform SH from the authored scene RGB at offsets 0/4/8,
then the same command/packer path retains it per object. Source-checked shader
pairs are:

| Surface family | Without runtime SH | With runtime SH |
| --- | --- | --- |
| Geometric normal / Fresnel | 220 | 222 |
| Normal map / Fresnel | 221 | 223 |
| Geometric normal / reflection | 210 | 213 |
| Normal map / reflection | 211 | 214 |
| Layered normal map / reflection | 212 | 215 |
| Geometric normal / no reflection | 300 | 303 |
| Normal map / no reflection | 301 | 304 |
| Layered normal map / no reflection | 302 | 305 |
| No directional, geometric normal | 356 | 357 |
| No directional, normal map | 356 | 368 |
| Cubemap reflection | 225 | 228 |
| Cubemap and vertex modulation | 226 | 229 |
| Restricted layered shadow family | 82 | 90 |
| Restricted local-light family | 317 | 319 |
| Sky | 362 | 363 |

These programs add SH to baked vertex illumination, not multiply or replace it.
For example pixel 223 forms directional diffuse plus vertex register v4, adds
packed SH and constant ambient, then constant illumination before base color.
Normal mapping still applies to SH when directional lighting is disabled.
The restricted inspection policy continues omitting its explicitly unsupported
shadow/local-light contribution; it retains the original SH contribution.

The separate ambient command `421ce0` captures object context `+2e0`; executor
`4271f0` uploads pixel 17. Its zero constructor/reset defaults are independent of
SH at object `+300`. Per-object packed coefficients prevent sky illumination
from leaking into ordinary world objects.

The installed shader package contains zero-bytecode publications resolving the
executable registry. There is no replacement `.xpu`/`.fx` shader. CF1I modifier
initialization has scale one and bias zero, and the scenario Lua has no hidden
sky material setter. Those earlier checks remain valid; the omitted runtime
feature explains why the correct source coefficients had no effect.

The correction is verified by GPU numerical checks and real stage captures.
The actual host checkpoint-10 screenshot (`slice3-host-checkpoint10.png`) now
shows textured cloudy sky and ground without the previous clipped white field.
The offscreen initial/exterior captures agree. Constant, quadratic, transformed
normal and additive baked-light cases pass in the existing native test suite;
strict build, six CTest entries and all 96 fatal clang-tidy entries pass. This
establishes the missing original irradiance behavior, not full pass-graph or
original executable image parity.
