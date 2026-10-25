# Placed lighting control-point observation

The receiver callback `5efe10` requests **control-point frame index zero** from
`428640` on the core model at object `+110`. It does not request skeleton joint
zero. Its returned matrix translation supplies local-light admission and the
retained strict SH/cube query. A false result falls back to object `+80` XYZ.
The earlier audit's “bone-zero” wording was wrong; a body-joint-zero lookup is
not an equivalent implementation and can reject valid placed assets.

## Original control lookup

`428640` scans the model's ordered control-resource list. The first resource
with the requested in-range non-null slot wins. Directory entries are byte offsets relative to that
resource, not absolute record pointers. Missing resource/directory slot
returns false. For that selected frame, `41d9f0/41da10` expose the authored
three points, signed joint indices and weights. When a joint palette exists and
there are influences, it transforms each point by each selected joint matrix
and accumulates in authored influence order. Weights are not normalized.
A signed index of -1 returns false immediately. Direct-world-joint mode uses
the stored matrices; ordinary mode obtains inverse-bind/skin matrices through
`422530`. With no palette or no influences it retains the authored points.

It constructs the frame basis from point1-minus-point0 and point2-minus-point0,
then sets translation to point0, W=1, and multiplies by the core-model matrix.
Lighting only consumes translation. Reconstructing that origin needs the first
point and its influences, not successful orthonormalization of unrelated axes or
other control slots. Invalid/out-of-range encoded memory is not a demonstrated
source fallback; the port must reject malformed selected data safely.

## Initial placed world ownership and fallback

`44a260` copies the **first placement matrix** into object `+50` and `+a0` before
creating physics and applying all body matrices through `5ef420`. Therefore its
object translation at `+80` is the first placement translation, regardless of
which skeleton joint that body's PHIT association names.

`5ef420 → 5f0e00 → 5f1080` establishes direct world-joint mode. `428600` returns
the core-model transform block; `5f0e00` sets its direct-world flag and overwrites
its model matrix with identity. `5f1080` publishes body world matrices to the
mapped skeleton joints. Accordingly, control-point weights on those joints
consume the same untransposed world matrices as the placed mesh palette source.
Do not apply inverse bind or the first placement matrix again. The fallback
still reads the separate outer object's first-placement translation.

A valid control influence not supplied by a body is not the same as source -1.
The original can retain an initialized/animated joint pose outside the body
association. This helper's bounded current contract requires every consumed
nonnegative joint to have a prepared body-world pose; it rejects an unresolved
one explicitly. Supporting such content requires supplying that real initialized
pose, not identity, a nearby joint or the first body's matrix. The current
content corpus must verify this admission boundary.

## Reconstruction location and existing helpers

`game/placed_lighting` owns the source compatibility query combining CPB slot
selection, PHIT body-to-joint identity and initial placement ownership. Shared
math owns point transforms and weighted accumulation. Scene lighting receives
only the resulting world position; it knows nothing about control points.

`animation::evaluate_control_points` correctly evaluates full prepared frames
for secondary animation, but its existing parser/evaluator validates every slot,
rejects signed -1 influences and requires nondegenerate full frame axes. Using
it unchanged would reject unrelated control records or confuse absent identity
frames with lookup failure. The placed helper therefore bounds and decodes only
the selected slot and evaluates only its consumed origin. It does not create a
second skeleton or cloth solver. If additional generic point-only consumers
appear, this selected-record query can move behind a shared control-point API.

`content::Builder` already retains optional `.controlpoint` resource locations.
`MissionLoading::resident_dependencies` must expose their borrowed bytes in
`ResidentModelView`, just as the player-body path does. Mission ownership keeps
them alive through placed scene preparation. Successful origin queries allocate
nothing and can be prepared once for the frozen initial placements.

Both header design and implementation consult Core I.1/SL.con.3 and performance
MEM.9. Contracts separate control-point and skeleton identities, bound every
selected offset/index, preserve explicit absence, and retain fixed scratch.
Original functions and instructions are in existing passes 54, 59, 81, 100,
137 and the ignored native-decompilation archive. No raw source is copied here.

## Verification and remaining admission boundary

The implementation preserves the existing emitter helper and adds the receiver
query beside it. Mission resident views now expose their already-retained
control-point bytes. In-process runtime contracts pass for weighted direct-world
transforms, non-unit weight sums, absent resource/slot, signed -1 fallback,
zero-influence authored origin, unrelated invalid slots, missing prepared joint,
nonfinite selected input, truncated selected data and inconsistent body counts.
The existing emitter cases also still pass. These are contract cases, not
rendered parity claims.

An independent Unicorn run executes the original unpacked Steam executable's
`428640` with synthetic resources and direct-world palette matrices. Six cases
agree: weighted and unnormalized XYZ, zero influences, absent resource, absent
slot and signed -1 failure. Return status is the original byte boolean. The
weighted two-joint observation is (-1, 9.25, 15.75), while non-unit weights yield
(0.25, 8, 13.25), matching the separate production contract fixtures exactly.
The outer-owner fallback remains established by the `5efe10/44a260` instruction
trace and is exercised by production tests. Witness script/results remain
ignored under `research/evidence/reconstruction/lighting-redesign/placed-control-point/`.
The executable SHA-256 is
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.

Earlier strict-field evidence compared 81 coefficients at three supplied lantern
BG joint-zero translations. It validates the field kernel at those supplied
positions; it does **not** establish that they were each original control-point
zero observations. The current scene corpus must be rerun through this adapter
before claiming the actual placed receiver positions are verified. An unresolved
nonnegative control influence still requires a real prepared pose rather than
invented lighting coordinates.

Focused fatal clang-tidy passes for the placed helper, resident-input adapter and
existing placed collision test translation unit after fixing the new braces
findings. The rebuilt runtime suite and repository policy/content checks pass.
The integrator owns the final full gate and original-content scene rerun; this
helper verification does not declare the lighting redesign complete.
