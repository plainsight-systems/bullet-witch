# Independent lighting redesign review

October 24, 2026. Review against accepted timing baseline `c4556f7`.

**Reopened after October 25 visual review.** The initial independent review
covered the complete uncommitted lighting redesign and finished graphics mip
path. Two production design findings and one evidence-tool finding were repaired
and independently rechecked below. The subsequent user screenshots show visible
speckling on Alicia and her weapon, so the combined live rendering path is not
accepted. R4 records the verification gap and the subsequently demonstrated
unit-direction and specular-bandwidth defects. Their native repairs have been
independently inspected; final output validation and user acceptance remain
distinct gates. The reviewer made no production
edits, game-installation changes, commits, pushes or visible game launches.

The scope is the [51-boundary acceptance map](lighting-redesign-acceptance.md),
not only the most recent mip patch. Review includes source fidelity, capability
ownership, lifetime and teardown, per-instance isolation, failure publication,
CPU/GPU concurrency, unsupported branches, and independence of test evidence.
Missing gameplay/event/weather/particle producers remain explicitly tracked
upstream work; a callable rendering operation does not implement its producer.

## Resolved findings

### R1 — P2: residency and scene publication share excessive mutation authority

At initial review, `include/bw/render/world.hpp:330` exposes model/texture upload,
binding and retirement together with retained draw/view/lighting/output mutation
through `WorldOutput`. `StageInspection` retains that full interface at
`src/game/stage_inspection.hpp:186`, although mission loading owns the resident
assets. Conversely, `MissionLoading` and its `ResidentDefinition` retain the
same full interface (`src/game/mission_loading.hpp:233–251`) although their job
is resource preparation and retirement, not scene publication or exposure.

The inspected call sites currently respect this division. The finding is an API
authority defect, not a claim that a texture was observed being freed twice.
A new call through either existing borrow can legally mutate the other owner's
state. This is the same class of unenforced ownership boundary that the timing
review repaired with a restricted pause capability.

Required repair: separate resource-residency commands from scene-publication
commands, narrow the actual retained borrower types, and keep the native backend
as the composition implementing both. Readiness observations may be a separate
const capability. Add compile-time restrictions for these real borrower types;
do not grant wider authority merely to make fixtures convenient. Preserve
candidate-before-publication and resource retirement after borrower replacement.

Status: implementation repaired and independently inspected. `WorldResources`
owns upload/bind/release commands; `WorldPresentation` owns retained scene
publication; both share only const `WorldObservation`. Actual Stage, mission,
resident-definition, PlayerActor and PlayerController borrows were narrowed,
not just their declarations. Owner copy/move restrictions and compile-time
negative capability checks are present. Independent final-build CTest passes
all six suites, including the native memory and resource capability contracts.

### R2 — P2: launch mip policy does not yet reach DDS sprite resources

The original `DDS ` resource constructor reads the global launch mip setting.
The implementation design currently captures that setting in mission options
for world, sky, actor, weapon and costume preparation. However,
`startup_title_surface.cpp` also loads `DDS ` records for title images and fonts,
and `startup_menu_surface.cpp` and `startup_loading_surface.cpp` do likewise.
Those paths upload through the base-only `SpriteOutput::upload_texture` API.

An enabled launch setting must either reach all consumers of that original
constructor or have a source-supported exclusion. A sprite sampler consuming
only level zero does not by itself exempt texture construction from the source
policy. The implementation lead has been asked to establish the actual source
boundary and complete the missing propagation if applicable. Generated media
or procedural textures are a different origin and must not be swept into the
`DDS ` policy without evidence.

The lead confirmed no source exception for these DDS consumers. Read-only
metadata census covered 91 sprite/concept archives and 435 DDS images; their
format counts were 73 format18, 361 format20 and one format15. The current
front-end group families include a format15 A4R4G4B4 font atlas and odd-sized
costume/menu images. Simply calling the existing world decoder would reject
those active inputs; simply wrapping the old RGBA pixels would lose their
source conversion policy. Shared base-surface conversion must precede the
complete mip chain. The packed controller-icon PNG path is separate from DDS
and must retain an explicit source contract.

Reproduction: the review's ignored
`research/evidence/reconstruction/lighting-independent-review/ui_dds_census.py`
uses the existing independent CPR reader and PNG chunk/CRC walker, reads the
original metadata at its retained record-relative offset and saves hashes and
dimensions in `ui-dds-census.json`. It writes no extracted images or game files.
The font is `sprite/font/font.cpr`, record1, 1024x1024; current custom art includes
499x535 inputs and option art includes 460x273 and 123x16 inputs. These are asset
definitions, not claims about all visible images in one frame.

Status: implementation repaired and independently inspected. Title, menu,
loading and font DDS resources preserve archive metadata through the common
decoder and typed upload; raw icon PNGs retain the separate base-only overload.
The host captures the launch setting by value, generates a candidate in the
source image's injected domain and publishes only after complete native upload. Original
four-bit staging and odd-dimension padding are represented. Mission preparation
captures the same launch value in const options. Independent final-build CTest
passes all six suites, including native level/face readback, accounting and
rollback. Final actual-content off/on corpus also passes against the same frozen
binary. Both source manifests still match current source bytes. R2 is resolved;
these controlled renderer observations do not claim an original live frame.

### R3 — P3: numerical witness must reject truncated comparisons

The ignored mip comparison driver paired input cases and native output lines
with `zip`, then likewise compared expected and actual components with `zip`.
Without exact length checks, missing lines or short payloads could be silently
excluded while the summary still advertised the input case count. No actual
truncation has been demonstrated; the defect is an incomplete verification
precondition, not a finding against the numerical filter itself.

Required repair: require exactly one native output per input case and exactly
matching output/expected byte extents before component comparison. The filter
author has been asked to add these checks before the final original-DLL rerun.

Status: resolved. The author added both guards and reran the original surface
entry. The independent reviewer then reran the portable executable and original
DLL separately for all 189 cases, 517 generated face-levels and 98,632
components (74,380 preserved base and 24,252 generated), checking exact line
counts and payload extents. Every byte matched.
The review's own result and binary/DLL hashes are saved in ignored
`research/evidence/reconstruction/lighting-independent-review/mip-replay.json`.

## Reopened integration investigation

### R4 — P2: prior raster witnesses did not cover the failing live composition

The reviewer inspected the user's October 25 04:52:07 and 04:54:37 screenshots
before tracing code. They show fine speckling across skin, clothing and weapon
surfaces in both cave and exterior views. The images establish a visible defect;
they do not identify whether its producer is pass-depth equality, shading,
shadow comparisons, overlapping geometry or another input.

Prior native coverage is narrower than that composition:

- `tests/macos_texture_allocation_test.mm::scene_preparation_contract` checks
  shared S/T ordering and surviving S color with constant clip depth `.75`, plus
  an unskinned triangle edge. Separate numerical vertex witnesses do not verify
  equal-depth raster replay through both complete native pipelines with live
  perspective/skinned inputs.
- `source_shadow_state_contract` explicitly publishes the production integer
  sample convention, then resets the draw list at exit. That reset clears the
  retained view. The later tilted self-caster, texel-phase and contact tests
  replace draws without republishing the production view; their useful existing
  assertions consequently run under the default half-integer convention. They
  use affine shadow W and isolated pixel observations, not the full composed
  warped-perspective/skinned surface.

The native preparation and material pipelines use the same vertex entry point
but are distinct pipeline states. Equality must be demonstrated at the actual
raster boundary; merely sharing source arithmetic is insufficient verification.
Shadow eligibility is a CPU whole-owner decision, whereas shadow comparisons
and S/T coverage happen per fragment. These are separate candidates to isolate,
not reasons to tune authored light values, depth bias or costume constants.

Status: investigation open. The implementation lead owns native reproduction;
the independent reviewer owns review of the demonstrated fix and regression
coverage. No cause or repair is claimed from the screenshots alone.

The lead's first combined headless witness rendered a full 128×128 target with
two joint influences, nontrivial transforms and projective W. S/T-only replay
matched, while enabling one planar primitive's self-casting changed 14 pixels
with preparation disabled. Independent inspection caught a fixture entry-path
mismatch before treating this as final production evidence: direct `encode_color`
does not apply the integer-center viewport owned by `begin_scene`; the shadow
pass does apply it in `prepare_shadows`. After the lead corrected the fixture
color encoder to the same +0.5 viewport offset, the self-shadow-only failure
remained at **10 pixels**. The reviewer inspected the corrected fixture and
`lighting-source-fidelity/noise-production-centers.txt`. This is a demonstrated
combined-path numerical difference with preparation disabled, not yet a source
fidelity failure: the original zero-bias independently rasterized float depths
do not establish an exact no-self-shadow guarantee. The ten-pixel witness and
compiler comparisons remain in ignored evidence; the required native contract
retains full-surface S/T replay equality. Moving the shadow experiment out of
required tests neither repairs nor accepts the screenshot artifacts.

Review handoff: native reproduction/fix belongs to `lighting_redesign`; material
and compiled S/T invariance investigation belongs to `material_surface_redesign`.
The independent reviewer found no demonstrated mismatch in retained caster/
receiver matrix or palette publication and no whole-owner sphere test that could
itself generate intra-triangle noise. Recheck the eventual fix against the
production-center full-frame witness and actual actor/weapon inputs. The prior
ignored `character-shadow/probe.mm` and its build manifest provide asset-backed
headless masks/caster isolation. Distinguish unclamped receiver slopes, clamp
hits, singular derivative fallback and real overlapping geometry; a fix for the
small planar precision counterexample alone cannot establish all-artifact
closure. No production fix has been reviewed or accepted at this handoff.

The resumed review inspected the actual-content probe and diagnostic shaders.
Its no-preparation switch intercepts the final view publication; caster switches
also intercept subsequent owner-visibility updates. The shader-level reception
bypass survives merged camera parameters. Marked/unmarked output equality
supports the unused-constant owner marker for the four fixed captures. These
checks validate their bounded isolation: they do not recreate live progression,
animation or output adaptation. The probe captures raw HDR at construction time
and bypasses the display/history pipeline.

The actual PW0/PW5 cave/checkpoint-10 comparisons do not currently connect actor
self-casting, S/T replay or the inferred slope correction to the dense noise:
their masked body/weapon outputs remain identical when those paths are changed.
The reviewer independently compared all four captures byte-for-byte with exact
payload-length checks and reproduced zero changed selected pixels for marked
baseline, S-off, actor-casters-off and source numerator/no-plane-correction.
Counts and results are retained in ignored
`lighting-independent-review/actual-isolation-replay.json`. The production
renderer source, shader source, Metal library and renderer archive still match
the probe manifest; the probe source itself has subsequently gained front-view
poses, so that earlier source hash is historical rather than current.
Reflection removal changes cave weapon highlights, but that identifies a
contribution, not an erroneous formula. Material/texture input attribution
continues. Original footage research documents shadow flicker without showing a
matching dense actor-pinprick artifact.

An additional diagnostic risk was reported before accepting the next numerical
oracle: byte-packing float bits into a HALF target is not lossless if the draw
retains source-alpha blending and the packed fourth byte becomes blend alpha.
Likewise, PW5's composited visibility histogram is not a raw eight-tap visibility
histogram. Restrict capture to proved opaque ownership or validate a separate
nonblending capture and its coverage; require exact field/record sizes and compare
captured material output against the native baseline before using it as evidence.
This concerns the investigation harness, not a demonstrated production defect.
The follow-up capture now records draw blend/phase/owner opacity and rejects
blended or partial-owner records; missing or truncated required captures fail.
The reviewer independently decoded all 276 fields for the seven accepted PW0
cave pixels and one PW5 front pixel, using a separate byte/float decoder. Each
accepted draw is explicitly opaque/Main at opacity one; every stored field
matches, and captured material output is bit-equal to baseline HALF output.
The capture hashes and checks are retained in
`lighting-independent-review/oracle-capture-replay.json`. This resolves the
packing concern for those eight records only. It does not establish correctness
for rejected blended samples. The reviewer also inspected and reran the original
PS123/PS47 instruction replay on those supplied fields: all eight results match
the production HALF outputs; maximum float difference is about 6.85e-7. Its
constant supplied samples and synthetic fully occluded shadow field deliberately
exclude original texture sampling, shadow rasterization and upstream receiver
selection. The checkpoint-10 actor remains in a shaded doorway; it is not yet
the sunlit court composition from the screenshot. Broader capture remains open.

The later sunlit capture's large directional specular packet was independently
traced upstream rather than accepted from shader arithmetic alone. Actual
original AREA selection/publication produces diffuse `(6.4,4.1,3.4,1)` and
specular `(640,410,340,1)` for row zero; cave row one produces
`(10,6.9,4,1)` and `(1000,690,400,1)`. Original `4829d0` maps channels 90–93
to stage diffuse and 94–97 to stage specular. `460660` copies those values into
the directional command; `422170` supplies zero attenuation to `546510`, whose
direct-copy branch preserves the float4 colors. `545ea0` uploads those blocks
at PS23/24 without color scaling. The reviewer freshly executed the actual
AREA witness and the original color-copy/upload routines, intercepting only
the D3D upload endpoint. Both packets match exactly. Reproduction and results
are saved under ignored `lighting-independent-review/directional_*` and
`directional-*-source-replay.json`. This rules out an invented native factor of
100 in that packet; it does not establish the correctness of every live input
or justify clamping source-authored highlights.

The reviewer independently reran the later 69-record sunlit replay with a
synthetic shadow field conditioned to each captured visibility. All selected
original programs execute exactly eight taps. Of 69 results, 68 match the
production HALF output exactly. At pixel `(578,483)`, source red
`0.13397216796875` and native red `0.1339721530675888` differ by one float ULP
(`1.490116119e-8`) across an exact HALF rounding tie; their stored HALF values
are `0.134033203125` and `0.1339111328125`. That discrepancy is retained, not
accepted by widening a tolerance. The largest float difference across the
whole set is `3.814697265625e-5`. The independent result is
`lighting-independent-review/actual-open-conditioned-replay.json`. Conditioning
the field isolates downstream arithmetic; it cannot verify the actual original
shadow texture, taps, coordinates or derivative calculations.

The final frozen-pose presentation witness uses the public production
`prepare_presentation`/`encode_presentation` path, forwarding the Stage's output
policy unchanged. It completes every command before readback and before the
next presentation, retains the scene before its borrowed resources retire,
and advances only supplied presentation intervals; WorldClock and actor pose
stay fixed. The reviewer checked the probe/source/library hashes, including
equality to the current host metallib, and all ten raw frame extents/hashes.
Each raw frame is exactly 1024×576×4 bytes. For both PW0 cave and PW5 sunlit
poses, saved frames 1, 119, 120, 179 and 180 are byte-identical. Bright selected
samples survive or saturate after the production tone/gamma/bloom path; this
does not support dismissing them as an artifact of the earlier raw-HDR preview.
These remain offscreen bytes, not an OS-composited capture or an original-game
frame comparison. Independent results are in `final-presentation-check.json`.

The reviewer also checked the two diagnostic alternatives against identical
geometry and exact opaque owner masks. Enabling the existing source mip option
changes both base and normal texture sampling, so its visible reduction is
texture-sampling sensitivity, not isolation of a normal-map defect. The separate
source-comparison shader changes only the depth comparison to the original
numerator form without the native inferred receiver-plane bias. It leaves the
cave frame byte-identical; the sunlit case changes 729 body and 22 weapon pixels
within the exact masks. Neither option is an original-image error oracle.
`receiver-option-check.json` records the independently checked extents, finite
values and counts. Both alternatives are ignored diagnostics. Production still
uses its prior receiver-plane inference and default-off mip policy; deciding
whether to remove that inference is explicitly separate from the repaired
pixel-projection boundary and from explaining the remaining speckles.

The later buffer isolation supplies a stronger actionable invariant. With
unit geometric normal `N`, tangent `T`, `d=N·T` and mapped direction
`n=z*N+x*T+y*(T×N)`, its squared length is
`z²+x²+y²*(1-d²)+2*z*x*d`. Thus individually normalized but nonorthogonal
interpolated basis vectors do not produce a unit shading normal. Independent
reconstruction of the lossless captured inputs finds PW5 `(475,429)` has length
1.04754 and a directional half-vector dot of 1.04502, which saturates before
the power operation. Normalizing the completed direction restores a cosine
dot of 0.997595 and the authored exponent-35 response of 0.91918. In the cave,
the selected weapon's actual local-light response at `(315,228)` changes from
one to 0.49477 under the same operation; a near-unit sample slightly increases.
This is not a uniform dimming factor. Results and the local/directional
distinction are recorded in `shading-normal-invariant.json`.

Final normalization preserves the mapped direction, without inventing a new
orthogonal tangent frame. It also restores the unit-normal precondition of
the reflection-vector formula. This repair is now implemented and independently
inspected: one normalization follows normal-map basis combination before all
lighting consumers. The native contract uses analytic outside-disk, sheared
basis and orthogonal-basis cosine expectations; the reviewer independently
reran the passing memory suite. Original shader arithmetic oracles remain
unchanged historical evidence. This repairs the direction invariant without
claiming all spatial/temporal speckling is resolved; actual PW5 highlights still
require the ongoing pixel-footprint filtering check.

### R5 — P2: receiver shadow projection moved across the raster boundary

The lead identified a source execution-order departure; independent review
confirms it. Effective key `00822690100` uses VS453/PS123, and
`c0802690100` uses VS482/PS47. Both original vertex programs publish
camera-relative world XYZ, normals, tangents and UVs; neither publishes the
shadow-position varying used by the native material shader. PS123 and PS47
construct homogeneous world XYZ after interpolation, then evaluate the screen
Z/W and texture XY/W dot products before jitter sampling and comparison.

Native `world_color.metal::world_color_vertex` instead evaluates
`shadow_clip_rows` at vertices and interpolates the result. Moving this linear
operation across raster interpolation is algebraically equivalent in real
arithmetic, but changes float execution and does not reproduce the recovered
pipeline boundary. Source `42ffc0` subtracts the camera position from model
translation; `451fa0` publishes the corresponding receiver screen and texture
matrices separately at pixel constants 30 and 34. A repair must preserve that
coordinate basis and publication, not merely multiply a world-space matrix by
the existing relative-position varying.

The reviewer also checked all 158 currently admitted production keys against
the original registry and program dumps: all 99 shadow-using keys (47 distinct
pixel programs) declare both receiver matrices and the interpolated world-position
input. Their 12 selected vertex programs have no shadow matrix constants. The
bounded census is saved as
`lighting-independent-review/pixel-shadow-projection-census.json`; it is not a
census of every original engine shader.

Status: source-backed repair implemented and independently inspected. The pure
`make_shadow_receiver_projection` prepares the two matrices from the supplied
world projection and camera origin. Original CPU execution supplies 96 expected
float values over three origins; the test compares their bits, including an
optimized build checked by the implementation agent. The reviewer independently
reexecuted the original matrix-publication witness. The material fragment now
projects the interpolated camera-relative world position; the caster transform
is unchanged.

Each retained fixed draw owns its receiver packet. A retained view owns optional
world/sky packets derived before publication; invalid active receiver bases
reject before replacing the previous view. Both view-reset paths clear these
caches. The renderer copies fragment constants into the encoder and retains no
borrow of a caller's temporary packet. Admission and consumption both use the
draw-owned reception enable, so later shared lighting updates cannot activate
an unvalidated packet. The world-axis-preserving camera-relative basis is an
explicit source contract, not a generic inverse-matrix fallback. Independent
CTest after the repair passes all six suites, including native rejection
rollback and full-frame skinned S/T replay. This establishes the repaired
publication boundary; it does not establish this departure as the cause of all
visible speckling. R4 remains open independently.

The reviewer also followed the cave weapon's normal/reflection path through
original VS482/PS47: normal and tangent are transformed without vertex
renormalization, normalized after interpolation, and combined with the decoded
normal-map XY and reconstructed Z. The native path preserves that sequence and
does not add a final normal renormalization. Original sampler initialization
`451770` explicitly uses clamp addressing and linear minification,
magnification and mip filtering for environment slot seven, matching its
separate native sampler. The four accepted cave weapon pixel records all have
black environment RGB with alpha one. Cube face/orientation therefore cannot
explain the bright contribution at those specific samples. This does not prove
normal-map filtering, actual raster derivatives, every live vertex/palette, or
the local-light producer matches an original running frame.

For the selected weapon program, VS482 reads `TEXCOORD1`, adds the first
texture-offset vector and publishes `TEXCOORD5.xy`; PS47 samples its normal map
at those coordinates. Native reads the same HALF2 usage and adds the same
material offset before interpolation. Original `564f70` changes surface slots
zero through three to anisotropic minification with maximum four, after
`451770` establishes wrap addressing and linear magnification/mip filtering.
The actor submission and Metal sampler express that same policy. This establishes
coordinate and sampler selection, not identical hardware anisotropic taps.
The actual WA0 draw-zero/material-one and PW5 draw-nine/material-fifteen
original material-publication witness leaves both VS texture-offset registers
at zero after global initialization and the complete selected material chains.
The reviewer checked those final registers and all nine compared material
packets in `uv-material-publication-check.json`. Native offset agreement is a
code/source trace because those offsets were not included in the lossless
fragment capture; no additional capture parity is claimed.

The reviewer also read the installed 36-byte `SystemSetting` record again:
its offset-eight flags are `0x6f1e52`, with both edge mask `0x04` and mip mask
`0x08` clear. Source `410750` writes edge checkbox `3f6` into bit index two;
the translation at `410b6d..410ba4` maps it to renderer mask `0x40`.
`lighting-independent-review/saved-display-policy.json` retains the file hash
and exact decode. Therefore the current default-off edge filter agrees with
this inspected configuration; that is not evidence about settings used in an
unmatched original-game video.

### Filter inventory for the denoiser question

The represented material/output chain does not provide evidence of a separate
missing lighting denoiser. Its identified operations have distinct contracts:

- `4632d0/4630f0` produce the R32 shadow target and
  `460920 → 44eaa0 → 565590` binds it at sampler 13. The represented pixel
  programs average eight jittered depth comparisons, four slices with two taps
  each. This spatial shadow filtering is already implemented; it does not
  reconstruct per-pixel lighting from preceding frames.
- Output builder `56d6a0` schedules luminance reduction, bloom extraction/blur
  and display mapping. `5712f0` retains scalar luminance history for exposure.
  That history is not a per-pixel radiance buffer. Bloom's threshold, soft knee,
  radial and Gaussian filters likewise have their own spatial purpose.
- Final `461db0 → 547f30` selects the optional spatial edge filter. Its recovered
  original algorithm is implemented, with a separately documented missing
  saved-setting producer; it is not an undiscovered always-active pass.
- AfterImage is an identified effect controlled by AREA 11/12 through
  `45e130 → 463a80`. Strength is zero in all three retained cave/exterior
  snapshots, so `461f00` skips it there. Its active effect implementation remains
  an explicit upstream boundary, not evidence to add a baseline denoiser.

Evidence: [shadow filter](shadow-filter-baseline.md),
[display output](world-display-output.md),
[final edge filter](lighting-final-edge-filter.md), and the
[full source audit](lighting-source-fidelity-investigation.md). This is a bounded
negative finding about those represented paths and snapshots, not a claim that
every unimplemented effect in the complete engine has been proved absent. A
denoiser would not substitute for demonstrating and fixing the speckle producer.

## Reopened engineering repair: established specular filtering contracts

The user directed the team to fix the reproduced native defect using standard
render debugging and concrete mature implementations. An unavailable matched
original frame is not a blocker to that repair. Source arithmetic agreement is
insufficient when texture preparation or shading bandwidth violates the sampling
contract. The current normal, derivative, material-component and final-output
buffers must identify the earliest unstable contribution; R4 remains open until
the resulting actual scene is corrected.

The independent reviewer inspected Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`, specifically:

- `MaterialTemplate.ush::GetRoughnessFromNormalCurvature` and its normal-curvature
  helper, consumed by `BasePassPixelShader.usf`: geometric normal variation is
  evaluated from center-interpolated normals. Centroid interpolation is
  explicitly unsuitable for this derivative estimate. This addresses geometric
  specular bandwidth; it does not establish a faithful texture-normal variance
  estimate for Bullet Witch's two-channel maps.
- `MaterialTemplate.ush::TransformTangentNormalToWorld` normalizes the completed
  world-space shading normal before lighting. Separately normalizing interpolated
  tangent and geometric normal does not make that basis orthogonal. The new
  actual-buffer diagnostic measures a tangent/normal dot of about 0.245 and
  final shading-normal length about 1.047 at a saturated PW5 highlight. This
  supplies a concrete general invariant to test before adding a variance
  subsystem: normalize the completed shading direction. It is an explicitly
  reference-based native adaptation if adopted, because the recovered original
  material equation does not contain that final normalization.
- `TextureCompressorModule.cpp::CompositeNormalLengthToRoughness`,
  `CompositeNormalToRoughness` and the associated normal mip construction:
  texture-normal variation survives as the length of an averaged full normal.
  Averaged mip normals remain unnormalized. The effective specular exponent
  decreases as that variance rises, instead of clamping light colors. Unreal's
  optional top-level preprocessing and configurable curvature coefficients are
  not unexplained constants to copy into this renderer.
- `MetalState.cpp::FindOrCreateSamplerState`: anisotropic sampling uses linear
  minification/magnification/mip filtering with a separate maximum anisotropy
  and explicit LOD limits. The current native mapping agrees; selection of an
  actual resident mip chain is a distinct resource decision.

NVIDIA's original [Toksvig paper](https://developer.download.nvidia.com/assets/gamedev/docs/Mipmapping_Normal_Maps.pdf)
gives a Blinn–Phong-compatible variance filter. For averaged-normal length
`L` and authored exponent `p`, factor `f=L/(L+p*(1-L))` gives exponent
`p'=f*p`; amplitude scales by `(p'+1)/(p+1)` to preserve energy. Uniform normals
retain the original lobe. Its [normal compression paper](https://download.nvidia.com/developer/Papers/2004/Bump_Map_Compression/Bump_Map_Compression.pdf)
explicitly identifies the limitation of reconstructing Z from two components:
the resulting unit length loses the variance signal. Therefore measuring the
length after our current XY reconstruction is not a valid Toksvig adaptation.

For a demonstrated texture-specular contributor, the repair must retain the
filtered full-normal information before reconstruction, or use another concrete
footprint integration method. It must preserve material identity, UV addressing
and resource lifetime and validate diffuse detail as well as highlight stability.
For geometric curvature, use the geometric basis and a correctly sampled
derivative instead. Neither route requires a postprocess denoiser or arbitrary
scene brightness reduction. Reference snapshots and hashes remain ignored under
`lighting-independent-review/unreal-aliasing/`.

### Footprint candidate: independent actual-content check

The diagnostic candidate estimates full unit-normal variation at four tensor
Gauss points in the pixel footprint, using offsets `±1/(2√3)` in each axis.
It samples the existing two-channel texture at those positions, reconstructs
each completed unit shading normal, and averages the resulting directions.
Centered squared deviations give `L²=1-variance`; this retains exact zero
variance for identical samples without an arbitrary epsilon. The average's
direction is normalized separately from that length. Toksvig exponent and
energy adjustments affect specular responses only. Zero exponent and zero mean
length have defined limits; authored light intensities and texture policy do
not change. This is a native sampling adaptation, not recovered original code.

The reviewer inspected and reran the strict comparison script independently;
all 15 result rows agree exactly. The reference averages 16 aligned high
resolution linear-HDR samples per pixel. Every sample must have the same opaque
owner, draw, primitive and shadow visibility as the low resolution pixel.
All options share the same mask. Temporal error subtracts the reference's own
change after a 1 mm camera translation, rather than treating legitimate moving
highlights as error. Blocks with any high-resolution diagnostic/production
disagreement are excluded. The results below compare the unit-normal repair
alone with the footprint candidate; they do not compare with a darker target.

| Region | Static/temporal pixels | Static RGB mean absolute error | Temporal residual RGB mean absolute error |
|---|---:|---:|---:|
| PW5 body | 1469 / 1141 | 0.13759 → 0.10644 | 0.16123 → 0.11829 |
| PW5 shoulder | 140 / 124 | 0.28363 → 0.15636 | 0.32889 → 0.23358 |
| PW0 weapon | 744 / 585 | 0.006160 → 0.003201 | 0.006146 → 0.002724 |

Full-output candidate images were inspected as well: small highlights soften
while the authored bright shoulder response remains. The bounded evidence
supports integration; it does not promise elimination of all aliasing, exact
pixel integration, or original live-frame correspondence. The approximation
retains center light/halfway vectors and uses four normal samples. The required
native tests, final production output and repository gate must still be checked
after integration. The independent replay is saved as ignored
`lighting-independent-review/strict-reference-replay.json`.

The integrated helper was then inspected directly. Its admission branch is
uniform for each draw, derivatives precede cutout discard, explicit texture
gradients avoid implicit derivatives after a lane's zero-geometry early return,
and one invocation-local moment estimate serves directional and local specular
consumers. Positive-power filtering skips unused specular draws. Mean direction,
variance length and exponent are separate values; no clamp to authored light
energy or new persistent state is introduced. Both initial submission and model
replacement validate consumed exponents before publishing retained draws;
lighting, view and pose update packets cannot mutate that material exponent.

Native tests exercise analytic mapped-normal directions, constant-normal
positive-power identity, the zero-power limit and negative-exponent rejection
with the previous retained draw intact. The reviewer independently reran all
six CTest suites against the stable integrated build: all passed (0.31s).
All 152 frozen source, library, script and buffer sizes/hashes also verified.
The reviewer independently checked the final production captures byte for byte:
both complete PW0/PW5 linear-HDR frames and all five completed presentation
frames match the frozen measured candidate exactly. Each raw frame contains
2,359,296 HALF components; each presentation frame contains 2,359,296 bytes.
The fresh candidate also matches the original diagnostic buffers exactly.
All 31 final comparison-file hashes verify, and the current built shader library
matches the captured library, SHA-256
`90363c2175410f9849050b397b5dd354468d0216095e958531f5be4080488b49`.
The final source snapshot differs from current source only in formatting
whitespace. Results are in ignored `final-filter-capture-check.json`.

There is no unresolved actionable implementation or ownership finding in this
repair. R4 remains open for the user's combined moving-scene visual acceptance;
the measured reduction and bounded captures do not claim perfect antialiasing.
The reviewer inspected the completed `surface-filtering-full-gate.log`: 335
formatted files, strict build, all six suites, all 185 fatal clang-tidy commands,
five policy tests and 658 text-only file versions pass. `git diff --check` also
passes. This gate supersedes the pre-filter receiver-projection gate for the
current production implementation. No checks or comparison tolerances were
weakened to obtain the result.

## Ownership trace

| Authority | Actual mutation and borrowing examined | Initial review result |
|---|---|---|
| Mission resource lifetime | `MissionLoading`, resident definitions, stable body/weapon candidates, read-only source views | Stable owner identity and candidate retirement remain explicit; R1 limits its renderer authority. |
| Environment progression | `Environment` owns channel transition state; Stage consumes accepted world frames and camera observations | No added lighting clock or mutable world-clock borrow. |
| Field/emitter state | Noncopyable/nonmovable `SceneLighting` owns prepared fields, intensities and local-light selection scratch | Updates and observations are serialized on the owning thread; returned packets own values. |
| Reflection data | Stage declares immutable reflection lookup before `SceneLighting` | The actual composition keeps the borrowed lookup alive and stationary until after scene destruction. |
| Placed receiver caches | Each placement owns a `RetainedLightingReceiver`, position/radius and packet range | Scene outlives the caches; specified instance IDs isolate copies of a resident model. |
| Actor and weapon observations | Stage reads independent transforms/bounds and const action visibility observation | No lighting sampler or exposure state moved into player controllers. |
| Native submission | Renderer owns copied constants, copied palettes, phase order and prepared destination index | Batch destination/value validation precedes writes; R1 restricts callers' unrelated capabilities. |
| Output lifecycle | Noncopyable/nonmovable postprocess owns measurements, history and targets | Tone gate, event reset, scene release and device lifetime are distinct; no borrowed mutable history. |
| CPU/GPU synchronization | Host completes the command buffer before subsequent world mutation; postprocess checks previous completion before readback/reuse | Current serial path is explicit. This is not proof of safe concurrent callers or an asynchronous renderer. |
| Texture construction | Launch policy, immutable mission options, portable candidate and native texture owner | Completed chain construction and accounting inspected; R2 propagation repaired, source conversion independently replayed, native tests and actual-content off/on corpus pass. |

## Guidance actually consulted

The reviewer queried both required MCP corpora for ownership, lifetime,
mutation, copy/move, GPU resource reuse and concurrency, and retrieved the full
applicable rules:

- Core `I.11`, `C.32`, `C.22`, `Con.3`, `CP.2`: distinguish ownership from
  borrowing, inspect retained pointers/references, preserve coherent owner
  operations, default observations to const, and avoid unsynchronized mutation.
- Performance `COPY.5`, `GPU.7`, `CONC.2`: unique native resources are move-only,
  resource reuse follows GPU completion, and any future cross-thread publication
  needs a real release/acquire or stronger synchronization edge.

These rules are review criteria, not automated proof. R1 was found by enumerating
the actual methods available through each borrow rather than trusting comments
or a passing lint run.

## Independent verification performed

- Read the complete uncommitted production changes against the accepted timing
  baseline, including the final typed DDS, capability split and native level
  count revisions. Reviewed source reports, original witness drivers and the
  production consumers, with all 51 dispositions below.
- Ran `ctest --test-dir build/host-debug --output-on-failure` after the finished
  implementation build: all six suites passed, including native Metal memory,
  image/font mip and renderer/resource contracts. This was the reviewer's run,
  not a repeated claim from the implementation report.
- Independently reran all 189 portable outputs against direct execution of the
  original DLL surface entry. Exact line/byte extents and all 517 generated
  face-levels match. Base preservation and generated component counts are
  distinguished under R3.
- Independently replayed original initial conversion for all 256 byte values
  into four-bit staging and three odd-dimension padding shapes with nonzero
  destination sentinels. Results are exact and saved under the review's ignored
  `base-conversion-replay.json`.
- Inspected the actual-content verification guard: the executable is hashed
  before launch, compared after completion and rejected if it changed. Reviewed
  final off/on summaries (123.00s and 185.91s) against the same then-current executable,
  SHA-256 `1bda84e1a4c19559e302417c7983585ad18e6c7a4e9168d8ba73855d32c34185`.
  Independently compared both recorded source manifests with file bytes at that
  handoff: neither had drift. Later R5 production edits supersede this hash.
  These tests decode original assets through production
  preparation and a controlled renderer; native pixels are a separate test.
  Results are under ignored `lighting-source-fidelity/mip-corpus-final-off/`
  and `mip-corpus-final-on/verification-summary.json`.
- Inspected the completed `python3 scripts/check.py all` log at
  `research/evidence/reconstruction/lighting-source-fidelity/mip-all-accepted.log`:
  334 formatted files, strict build, six passing suites, all 184 fatal
  clang-tidy commands passing, policy checks and 653 text-only file versions.
  After the final fixture-only rebuild, independently reran all six CTest suites
  again against the stable final build; all passed (0.34s). No check was
  suppressed and no expected outcome was weakened to close a finding.
- `git diff --check` passes. No production edit, commit, push or visible game
  launch was performed by the reviewer.
- After the R5 repair, independently reran all six CTest suites (0.34s),
  inspected the completed `lighting-noise-material/pixel-projection-full-gate-final.log`
  through fatal tidy and the 657-file-version content check, and repeated the
  independent matrix, material-publication and conditioned-pixel checks described
  above. These later checks supersede the earlier mip handoff for the modified
  receiver path; they do not convert R4 into visual acceptance.

## Evidence and completion gate

### Full audit coverage

The table groups related rows for readability but covers every audit identifier.
It records code/evidence inspection, not an assertion that every original live
producer has been implemented or every visual result accepted.

| Audit rows | Independent inspection and retained boundary |
|---|---|
| L01–L07 | Followed mission identities into Stage, camera-selected environment publication, accepted root cadence, channel values and separately owned field intensities. Genuine world/entity/event producers remain upstream. |
| L08–L13 | Checked blended SH versus strict prop sampling, basis/scaling, terrain/sky policies, authored directional construction, view versus model light selection and residual SH addition. Source-execution witnesses cover supplied inputs; dynamic emitter ownership remains upstream. |
| L14–L22 | Checked exact effective-key registry and explicit shader dependencies, declarations, all influence counts through shared color/preparation/shadow deformation, normal sampling, material binding and hair algebra. Inspected the independent original-instruction oracle and production-shader witness rather than treating the reconstructed equations as their own expected results. Matched live original palettes remain absent. |
| L23–L26 | Checked distinct T final-alpha and S/depth cutout contracts, native formats/samplers and accepted material-animation time. R2's missing DDS consumers were repaired; complete filtering/conversion, native residency and both content modes are verified. Original-device comparison precision and physical display transfer remain bounded. |
| L27–L31 | Checked source projection separately from caster-selection volume, owner eligibility/force state, setup retention/cull/bias publication and reception dependencies. Native guard/receiver-plane inference, D24 versus Depth32 precision and original unsupported address-six behavior remain explicit. |
| L32–L34 | Checked static sky transforms, fog factors and shared-owner S-before-T replay on the restored HDR/depth targets. Static sky is not evidence of implemented storm controllers; native chunk phase is not owner-opacity routing. |
| L35–L42 | Checked clear-color publication, pre-tone/meter/bloom/final algebra, independent geometry/fullscreen sampling conventions and completed-measurement exposure ownership. Tone toggle, history reset, scene release and lifetime reset remain distinct. Initial original surface contents and display correspondence are not recovered by same-input native tests. |
| L43–L46 | Followed original control-point-zero query into per-instance strict caches, independent local refresh, reflection slot selection and authored binding precedence; checked camera ownership of AREA far distance. Missing prepared nonnegative control influences reject explicitly. Source asset census and supplied original sampler positions do not establish live instance state. |
| L47 | Confirmed the missing event AREA save/restore, storm and particle producers remain named implementation dependencies, not silently implemented by generic lighting values. |
| L48 | Retained the unresolved original D3D sampler-call result; no code/source search can replace the absent driver observation. |
| L49 | Checked explicit owner-opacity routing, complete S/T replay, alpha reference, atomic retained updates and independent zero-opacity caster membership. Gameplay fade producers remain upstream. |
| L50 | Checked final-edge filter admission/consumer and independent source/native witness. Saved-setting producer and optional world/mist/elemental composition owners remain open. |
| L51 | Whole-frame PW0/PW5 and moving/paused scene acceptance still requires the user walkthrough. There is no matched original frame/camera/palette/output capture, and this review does not manufacture that evidence. |

The review has read the material, receiver, reflection, overflow, shadow-owner,
control-point, owner-opacity, output-lifecycle and final-edge source reports and
their production consumers. It distinguishes exact supported source-program
admission from guessed fallback; supplied-input original instruction execution
from reconstructed device raster tests; and both from a matched original live
frame. Initial source evidence supports the intentional strict versus blended
receiver split, complete influence-count decoding, separate S/T coverage,
Main/Trans ordering and distinct output reset lifetimes.

The initial mip handoff, final-diff review, R1–R3 repairs, independent rechecks
and repository gates completed before the user's visual review. R4 now reopens
the combined rendering acceptance; those earlier passes do not dismiss the
visible defect. The current renderer
is explicitly serial; this review does not certify concurrent calls or future
asynchronous resource retirement.

The remaining acceptance is the user's PW0/PW5 and scene/prop/sky walkthrough,
including moving/paused transitions. Original matched live frame, palette,
exposure history, first luminance contents, physical display transfer and
unsupported sampler-driver behavior remain evidence limits. Genuine
world/entity/event startup, dynamic emitters and fades, storm/particle producers,
optional effect composition and original settings UI/persistence remain named
upstream implementation work. These do not conceal another repaired-current-path
finding, and this review does not mark those future owners implemented.

## Subsequent user acceptance — October 25, 2026

After this independent review, the user accepted the implemented lighting
baseline and authorized committing it. The combined moving-scene acceptance
obligation recorded under R4 is closed by that user decision. Historical
findings, independent verification and original-device evidence limits above
remain unchanged. Residual angle-dependent speckling and coarse shadows are
accepted limitations, plausibly original-era behavior but not established
original-image correspondence. The explicit native normalization/filtering
adaptations remain recorded. Further visual-quality work is deferred to the
upgrade pass unless a concrete new implementation defect appears. See the
acceptance ledger for the 7:18:15 a.m. cave and 7:19:52 a.m. outdoor school-outfit
reproduction references; no screenshots are tracked.
