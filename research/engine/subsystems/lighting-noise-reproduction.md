# Lighting noise: reopened visual acceptance

The user rejected the first visual walkthrough: dense stippling on exposed skin
and clothing outdoors, with bright pinpricks on Alicia and her weapon indoors.
Both screenshots were inspected. They establish a visible defect, not its cause.
The prior code review and numerical checks remain evidence for their own scopes;
they do not close this failed visual acceptance.

## Competing causes and discriminating checks

- Preparation/color depth replay: the same geometry is drawn by separate native
  pipelines with GREATER_EQUAL. Shared vertex source does not prove compiled
  invariance. A new headless contract uses the production renderer and shaders,
  nonconstant clip depth/w, nontrivial joint transforms and a complete-frame
  comparison of color-only against preparation-plus-color. Shadows, spatial
  material variation and output adaptation are absent, isolating replay.
- Shadow self-reception: source jitter, integer pixel centers, depth bias and
  warped projection must be exercised together. Existing affine center-pixel
  checks do not cover the combined production path; some fixture submissions
  reset the view and therefore also reset the integer-center flag.
- Material/normal/specular numerical behavior: supplied-input equality is not
  evidence that live inputs and coverage match. Trace without brightness clamps
  or denoisers and retain any independent defect.

The initial regression is authored geometry inside the existing native memory
scenario. It adds no integration scenario or production mutation/readback API.
RAII allocations survive command completion before bounded diagnostic readback.
Both C++ corpora were consulted for design and implementation (E.6, GPU.1).
No visible game launch or unsolicited input is part of this investigation.

## Native reproduction status

The 128×128 warped, two-influence triangle has ten differing pixels when its
shadow casting is enabled, with S preparation disabled. Color and shadow
viewports both use the production +0.5 offset and the view is republished after
each draw replacement. The preceding S-on/S-off check passes exactly. A first
diagnostic omitted the color viewport offset and differed at fourteen pixels;
that is superseded by the ten-pixel production-convention result. Neither
result attributes every screenshot artifact to this mechanism.

The prior sloped fixtures reset their view through `set_world_draws`, then did
not restore integer sampling; their assertions therefore did not cover this
combined path. Current evidence is retained under
`research/evidence/reconstruction/lighting-source-fidelity/noise-production-centers.txt`.
Actual actor and weapon draws are being isolated independently before closure.

## Controlled arithmetic and compiler comparison

Ignored, separately compiled shader variants retain the same triangle and native
submission. The production arithmetic differs at ten pixels; adding both Metal
position invariance and its compiler preservation option still differs at ten.
Disabling fast math differs at five. Restoring the source numerator-form shadow
comparison while retaining the inferred receiver-plane correction differs at
sixty (fifty-one with fast math disabled). Results are in
`lighting-noise-material/shadow-compiler-variants.json`.

These outcomes expose precision sensitivity; they do not establish a missing
bias or justify forcing exact self-shadow equality. The source zero-bias float
algorithm may itself produce these samples. The no-self-shadow assertion is an
investigation witness, not yet an established source acceptance contract.
Actual actor/weapon comparisons and the original zero-correction comparison
remain necessary to attribute the user's dense noise.

The pinned Unreal directional virtual-shadow implementation was re-read at
`f45729c0b6f097038e72b52d366a62baeca3275b`. Its slope correction is part of a
ray-start calculation, including a separate ray offset and orthographic clipmap
projection. The current perspective-warp adaptation is explicitly inferred;
matching its small formula does not prove equivalent coverage or light leakage.

## Actual-content attribution, first bounded results

The offscreen current-renderer probe admits PW0 and PW5 at the initial cave and
checkpoint-10 doorway poses. A diagnostic owner marker is byte-identical to the
unmarked HDR capture. Across all four captures, toggling S preparation or actor
shadow casting changes zero masked body/weapon pixels. Original numerator-order
shadow comparison with no inferred receiver-plane correction also changes zero
masked body/weapon pixels. This does not support blaming S/T or that correction
for the reproduced actor highlights.

Disabling world casting or actor shadow reception changes broad shaded areas,
including legitimate shade; those changed-pixel totals are not defect counts.
Slope-clamp hits are sparse, derivatives remain finite, and no singular fallback
is observed on the masked actors. Source-vs-native material/highlight attribution
continues. These are fixed-pose, raw HDR captures, not tone-mapped screenshots or
original-device frame parity.

The no-self-shadow diagnostic is retained in the ignored standalone harness,
not the required contract suite. The source does not guarantee exact equality
across independently rasterized zero-bias float depths. This observation is not
fixed or erased; the production native suite retains the source-backed full
surface S/T invariance assertion.

## Aliasing reference boundary

The retained original device creation (`4122c0`) zero-initializes its 56-byte
presentation structure, then assigns its named fields; the multisample type and
quality words remain zero in the observed constructor. This is a bounded device
construction trace, not proof about every later offscreen target. It does not
support adding an untraced temporal antialiaser or lighting denoiser to explain
these samples. The separate original-footage report records shadow artifacts
in reviews but does not demonstrate the same dense actor/weapon pinpricks.

## Actual captured inputs versus original pixel instructions

Eight accepted opaque pixels (three PW0 body, four PW0 cave weapon, one PW5
front/low-angle weapon) were captured by losslessly packing float32 bits into
byte-valued half channels. Acceptance requires exact payload lengths, integer
bytes, an explicitly opaque Main draw with owner opacity one, and a captured
final shader result reproducing the untouched HDR half-pixel value exactly.
Blended and changed-output captures are excluded.

The original PS123/PS47 instructions were independently replayed with those
actual interpolants, sampled textures, final material/light constants and native
camera-relative local-light positions. All eight source results reproduce the
same HALF colors; the largest float arithmetic difference is 0.000000685453415.
The cave weapon highlights include one local light while directional shadow
visibility is zero. Source arithmetic therefore reproduces those supplied-input
highlights; this does not prove original texture bindings, light selection,
original-device sampling or complete frame equality.

The checkpoint-10 captures remain under the doorway roof. Normal-front and
low-front camera variants change one and twenty-three body pixels respectively
when restoring source shadow comparison; weapon pixels remain unchanged. These
are not yet the user's sunlit courtyard. An explicitly translated diagnostic
actor/camera pose is being added to cover that missing observation.

## Confirmed receiver execution-order departure

Original receiver programs reconstruct shadow projection per pixel from the
interpolated camera-relative world position. The full current registry census
covers 158 admitted keys: all 99 shadow keys / 47 pixel programs use both pixel
matrix blocks; their 12 vertex programs do not contain shadow matrix constants.
The prior native implementation instead projected in the vertex stage. This is
a demonstrated source departure, independently of screenshot attribution.

Original `451fa0` translates the projection by camera origin on the CPU, then
constructs a separate texture-remapped matrix; `42ffc0` preserves world axes and
subtracts camera origin from the model translation. Independently executing
`451fa0` and `406690`, intercepting only constant uploads, confirms both matrices
at three camera origins. Ignored evidence: `lighting-noise-material/receiver_matrix_witness.*`
and `lighting-independent-review/receiver-matrix-source-replay.json`.

Design: a pure fixed-size receiver projection value, prepared once per accepted
view or fixed draw. Native ownership validates the world-aligned relative basis
only for effective enabled shadow receivers, before atomic publication. The GPU
computes separate screen Z/W and texture X/Y/W from the interpolated position.
Caster projection and the separately identified native footprint inference are
unchanged by this correction. This narrows one source mismatch; it does not
claim that all cave highlights or the user screenshots are resolved.

Diagnostic pixel-projection captures changed 590 sunlit body pixels with the
existing footprint inference and 875 with source comparison; cave captures were
unchanged. Thus further material/input/display investigation remains necessary.

### Implemented receiver boundary and bounded verification

`make_shadow_receiver_projection` now preserves source CPU float multiply/add
rounding and publishes separate screen/texture rows. All 96 floats captured
from original matrix publication at three camera origins match bit-for-bit,
including an optimized standalone build. Finite input and overflow rejection
leave caller-owned inputs intact. The renderer retains derived values by view
or fixed draw, validates active receiver coordinates before commit, and clears
the derived packets with their owning view. The pixel shader uses these values
after camera-relative position interpolation. Caster projection is unchanged.

The native memory suite passes the new rejected-view/rejected-submission
rollback checks and existing shadow scenarios. The full-frame S/T regression
also passes. Full required validation is recorded separately after its final
run; an initial lint pass found an unchecked array index and unnamed buffer
slot, both corrected without suppression.

Production headless captures after this correction retain a bit-identical cave
frame. The sunlit diagnostic PW5 pose changes 583 body pixels and three weapon
pixels. The production CPU-published matrices intentionally differ from the
prior diagnostic that reconstructed uniforms inside the fragment. These are
raw HDR fixed-pose captures, not the complete live presentation or matching user
screenshots. Capture hashes and comparisons are in
`lighting-noise-material/receiver-production-captures.json` and
`receiver-production-comparison.json`.

An additional 45 accepted sunlit pixels with uniform shadow visibility (44 lit,
one occluded) reproduce exactly at HALF output when original pixel instructions
consume the captured inputs; maximum pre-HALF float difference is 3.815e-5.
The 24 fractional-visibility records were explicitly excluded from this check.
This includes bright skin samples, but proves arithmetic for supplied inputs,
not original input selection, sampling, final presentation, or artifact parity.
The original directional AREA specular packet and its subsequent PS upload were
independently replayed with identical values; lowering those values would be an
unsupported appearance adjustment.

The independent normal/tangent trace confirms the source transforms the basis
without vertex normalization, normalizes the interpolated basis in the pixel
program, reconstructs the normal-map Z term, and does not renormalize the final
basis combination. Four accepted cave weapon samples have black cubemap RGB;
their highlights therefore require tracing local reflected-light inputs rather
than assuming a cubemap-face error. Material and local-light packet provenance
remain under investigation. Visual acceptance remains open.

### Source-owned input follow-through and option isolation

Original material command chains for actual PW5 body draw 9 / material 15 and
WA0 weapon draw 0 / material 1 reproduce the captured specular, reflection,
Fresnel, environment-power, material-color and consumed illumination values.
Original default publication supplies zero ambient and UV offsets and unit
indirect colors. The original lantern record through CPU light construction and
PS upload also reproduces all 15 consumed local-light values in the accepted
cave weapon sample, including specular `(170,140,130)`. These are source-owned
input witnesses rather than another comparison using supplied native values.
Details and limits are in [the material investigation](lighting-noise-material-investigation.md).

Original UV declarations and offset publication agree with the normal-map input;
surface wrap, linear magnification/mip filtering and anisotropy four also agree.
Hardware anisotropic footprint/derivative equivalence remains unverified.
The inspected saved settings disable both mip generation (mask `0x08`) and final
edge filtering (mask `0x04`, bit index 2); neither is an accidentally omitted
always-enabled source pass.

Two further headless variants retain the same geometry/camera and current CPU
receiver matrices. Source numerator comparison without the native receiver-plane
inference leaves the cave frame bit-identical and still exhibits shoulder
stippling outdoors. It changes 729 exact-opaque body-mask pixels and 22 weapon
pixels in the sunlit pose. This does not justify claiming the inferred footprint
policy caused every screenshot artifact or silently removing it.

The separately implemented **original optional mip setting** reduces visible
texture aliasing. It changes cave weapon samples by up to 0.5304 HDR and
sunlit body samples by up to 30.809 HDR without changing authored coefficients.
It does not remove all strong shoulder highlights. This is evidence of texture
sampling sensitivity, not proof that the remaining screenshot appearance is
original or permission to enable the option by default. The first preliminary
mip capture used the older doorway probe source; it is retained under a
`doorway-mips-preliminary` name and excluded from this comparison. Final captures
use the same marked probe source as the production comparison, with only the
explicit mip option changed. `receiver-mips-captures.json`,
`receiver-source-captures.json`, and `compare-receiver-options.py` preserve hashes,
commands, exact extents and exact-opaque mask criteria.

All 69 sunlit accepted pixels were also replayed through original instructions
with a synthetic eight-tap field conditioned to measured visibility. Each
program consumed exactly eight taps. 68 round to identical HALF; one red channel
at `(578,483)` differs by one HALF step because source `0.13397216796875` is an
exact rounding tie while native `0.1339721530675888` is just below it. The float
difference is `1.490116119e-8`; no tolerance was widened. This remains a bounded
arithmetic witness, not validation of original shadow sampling or a full frame.

The final required gate for the receiver correction passes: 335 formatted files,
185 fatal clang-tidy translation units, all six test suites, and the content
policy over 657 file versions. The independent reviewer reran the native suites
and inspected ownership/source witnesses. Source/frame attribution and user
visual acceptance remain open; no original-device frame parity is claimed.

### Full presentation follow-through

The final frozen-scene probe now executes the production exposure, gamma, bloom
and display path through public renderer capabilities. It retains the same
inputs at 60 presentation snapshots per second through frame 180, beyond the
64-sample / 30 Hz history window, without advancing world state. Each GPU command
completes before reuse. Frames 1, 120, 179 and 180 are byte-identical in both
scenes. Strong highlights remain visible: the named sunlit body sample becomes
white; the named cave weapon sample becomes `(255,191,133)`.

This closes the raw-HDR-versus-presentation diagnostic gap, but is not original
frame matching. The exact output bytes, source/shader hashes and ownership method
are in [the material report](lighting-noise-material-investigation.md#production-presentation-of-the-frozen-diagnostic-scenes).
No further demonstrated source equation or supplied material/light input error
was found in these bounded traces. Remaining original hardware sampling,
matched live actor state, and screenshot-level attribution are explicit evidence
limits. The visible user report remains open rather than being relabeled as a
proven original-game artifact. No denoiser, light clamp or default option change
was introduced.

### Demonstrated native surface repair

The subsequent contribution isolation found nonunit mapped directions and
within-triangle normal/specular aliasing. The native unit-direction and
pixel-footprint moment filter are implemented, using pinned Unreal's direction
boundary and the published Toksvig legacy-power equations. This is explicitly
a native adaptation; original light/material values and default options remain
unchanged. Complete production raw/display frames match the independently
checked candidate. [Surface filtering](lighting-surface-filtering.md) records
the mechanism, ownership, measured static/motion improvements and remaining
approximation limits. Original-frame matching is not a blocker for this repair.

## User acceptance — October 25, 2026

The user accepted the current lighting implementation and authorized its commit.
Residual angle-dependent speckling and coarse shadows are accepted baseline
limitations. They may be consistent with limitations of the original renderer,
but no matched original-image correspondence was established. The explicit
native final-normal normalization and pixel-footprint specular filtering remain
documented departures from the recovered equation, not claims of original
behavior.

The latest reproduction references are the user's October 25 screenshots at
7:18:15 a.m. (cave) and 7:19:52 a.m. (outdoor school outfit). They preserve the
viewing contexts for the remaining angle-dependent appearance; the screenshots
and extracted data remain outside Git. No new visual investigation is required
for this accepted slice. Residual visual quality belongs to the later upgrade
pass unless a concrete new implementation defect is demonstrated.

Acceptance closes this represented lighting slice, not the named upstream
mission/event/weather/particle/settings producers or every original-device
evidence limit. The independent implementation review and passing verification
remain distinct from this user decision. Production code is unchanged since
the final passing full gate; this acceptance update is documentation only.
