# Player collision and the physics backend

Updated October 23, 2026. This is the source map and implementation contract for
the slice 5 custom static-contact boundary. Parent: [physics and collision](physics-and-collision.md).

## Decision and immediate consequence

Reconstruct Alicia's original character movement and collision rules in shared
engine/game code. Keep the rigid-body scene behind a separate backend contract.
Do not replace the character controller with a stock PhysX or Jolt character
controller merely because a replacement rigid-body library is needed.

The selected stage already supplies collision triangles. It does not require an
invented terrain heightmap or a flat floor. The `StaticCollision` owner now prepares static character contact geometry and
resolves proposed movement, capsule penetration and support, in addition to
camera clearance. Game state owns input, gravity, jump transitions and animation.
Actual stage traversal acceptance is separate from the in-process geometry tests;
passing the latter alone does not establish native gameplay parity.

The user approved retaining original custom movement while resolving the dynamic
physics backend separately. No replacement backend has been selected here.

## Recovered original ownership

The player factory `0x5fe970` calls base actor constructor `0x5fe070`, then
`0x5ff170`. The latter installs the original `witch::Runner` through
`0x5fd920` / `0x663c50`. This is the same component construction traced for
ordinary actors, rather than an assumption that player motion uses that route.
Runner retains the model at +0x24 and transform helper at +0x28. Its motion setup
`0x668230` also configures named bone handling including `Waist_direction`.

Runner handlers such as `0x664290`, `0x6659e0` and `0x666550` call the actor helper
`0x5ff410`. It sends a motion-related message when applicable, then calls
`0x6030d0`. The latter is a concrete character collision/support resolver. Its
observable sequence is:

1. Start from proposed actor translation at +0xd0 and retained previous
   translation at +0x11d0. Read shape parameters from actor definition +0x124c.
2. Build a movement-enclosing candidate bound with `0x600c30`, then fill the
   world-owned candidate list +0xa8678 through `0x5f4b20` / `0x5f4b50`.
3. Test the previous-to-proposed segment with `0x5f3a40`.
4. Resolve shape penetration through `0x5f3870`: the zero-height branch constructs
   a sphere (shape kind 8, `0x548660`); the positive-height branch constructs an
   upright segment with radius (kind 11, `0x5483c0`) and uses `0x5f30c0`.
5. Run the lower sphere/contact classification with `0x5f2830`; collect contact
   position and normal. This path uses the engine's shape dispatcher
   `0x54b380`, candidate objects and object flags.
6. Probe downward with another segment, evaluate the surface normal and limited
   correction branches, record a supporting object where the source permits it,
   and write corrected actor translation back to +0xd0..+0xdc.
7. If the returned support state requires it, `0x5ff410` requests a state created
   by `0x5fdb30` / `0x5fec70`. Original RTTI identifies that state as
   `witch::StateJump`; loss of support therefore enters the same named state
   family, and does not imply the user pressed jump.

This is custom engine collision and motion policy, not a call to
`NxController::move`. It includes more than a ray down to find ground. The existing
camera query is one reusable piece, not the complete controller.

The shape resolver also references actor flags and a supporting-object record.
`0x5f4b50` gathers registered world candidates and additional mutable-object lists.
The previous actor map separately establishes moving-parent correction
`0x600640` and actor-pair separation `0x5ffa30`. Static support can therefore be
implemented independently, but dynamic-platform following and collision with
moving props cannot be called complete by freezing those objects.

### Numerical interpretation still to preserve

The decompiler mis-types several status integers as floats. In particular, its
`1.4013e-45` comparisons in `0x6030d0` / `0x5f2830` are the bits of integer 1,
not a meaningful physical epsilon. Confirm the instructions before translating
these tests. Similarly, `thiscall` receivers omitted from apparent arguments
must be recovered before declaring an API.

Verified constant reads in the resolver include 1/30 second, 7.5, 0.3,
0.2999 and 0.01. The expression involving `max(1/30 - dt, 0) * 7.5` adjusts a
collision probe; it is not evidence that gravity equals 7.5 or that the whole
engine runs at a fixed 30 Hz. Do not transplant an attractive constant into a
different role.

## Collision assets and cooking

The installed stage's `.chit` / `.ehit` inputs contain bounded part directories,
float3 vertices and faces consisting of three uint16 indices plus a uint16 flag.
The existing decoder and camera registration consume these authored triangles.
Combined `.hit` precedence and name/category filters remain significant. See
[stage content](stage-content-decoding.md) and
[camera collision](player-camera-initialization.md#static-camera-clearance-reconstruction).

For the separate PhysX world, `0x4e7900` selects behavior by original name prefixes
and sends eligible geometry to `0x4f7ac0`. That helper constructs a triangle
descriptor from raw counts/pointers. `0x4f79f0` explicitly calls
`NxInitCooking`, `NxCookTriangleMesh`, creates a mesh through the SDK, then closes
cooking and retires temporary stream storage. The PC import table also contains
`NxCookConvexMesh`.

Thus at least this established world route cooks raw geometry at runtime. We are
not blocked on decoding only an opaque PhysX 2.6.4 triangle cache. This does not
prove that every `.phit` resource is fully decoded or that all convex, joint,
material and body schemas are ready for a replacement backend. The current
`PhysicsCollisionDefinition` validates a pointer graph and body/joint mapping;
it is not yet a typed physics scene description. No heightfield creation route
was established in this trace.

## Animation is one consumer of movement

Animation evaluation produces a skeletal pose and motion events. Physical
jumping additionally requires the state transition, original velocity/gravity
rules, collision response, support-loss detection and landing transition.
Displaying a jump clip while an actor remains at a fixed inspection transform
does not implement a jump. Conversely, geometry collision alone does not select
the correct clip or blend.

Keep proposed movement, resolved actor transform, grounded/airborne state and
motion events explicit at their owner boundary. Preserve any recovered motion
displacement contribution; do not apply both root motion and an invented input
velocity to move the actor twice. Actor-specific policy belongs above generic
pose evaluation and generic collision geometry.

### Recovered airborne update

The dispatch registration at `0x667910` binds Runner / StateJump entry and update
through `0x65f140` / `0x65f3c0`. The thunks dispatch Runner vtable +0x70 / +0x6c,
which resolve to `0x664480` / `0x6644b0` in vtable `0x8560f8`.

Entry selects motion `(stateMotionWord & 0xf) | 0x290` with blend argument 0.3.
The update instructions establish the following order (Y is up):

```text
newVy = oldVy - gravity * dt
proposedX += vx * dt
proposedY += (oldVy + newVy) * dt * 0.5
proposedZ += vz * dt
resolve airborne contacts and support with 0x6026c0
choose landing/state continuation from that result
```

The gravity global at `0x9775ec` is initialized in the file to
9.800000190734863. This is a directly used gravity value, unlike the earlier
7.5 probe correction. Its runtime mutability must be retained until write sites
are fully checked. When airborne collision reports no landing, the update feeds
resolved horizontal displacement back toward stored velocity with factor
`4 * dt` and updates orientation/model state. When support is reported it selects
a transition according to downward velocity, including a threshold of
`gravity * -0.3`; this is not a generic unconditional switch to idle.

`0x6026c0` gathers candidates using a movement bound enlarged by 0.5, applies
previous-to-proposed segment and sphere/capsule correction, performs a downward
support test and writes resolved actor translation. Jump initiation, the exact
landing-state identity and every flag branch still need their immediate
receivers mapped. The generic airborne integrator and its separation from the
pose sampler are now source-backed.

## Reference implementations inspected

Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`:
read `PhysWalking`, `PhysFalling` and `ComputeFloorDist`. The source separates
movement state/integration from shape queries, checks support using volume sweeps
and qualified line probes, handles initial penetration and floor edge tolerance,
and distinguishes landing from sliding against a wall. Falling evaluates bounded
time steps, gravity/root motion and swept displacement before publishing a result.
These are useful boundary and verification patterns. Its constants, capsule
algorithm and state machine are not substituted for the recovered Bullet Witch
resolver. The local source excerpt remains ignored evidence, not production code.

NVIDIA's [character-controller documentation](https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/CharacterControllers.html)
also distinguishes displacement-based movement from teleportation. Gravity is
supplied by the caller, and collision results can drive animation transitions.
Its stock CCT is one possible implementation, not proof Bullet Witch used it.

## Backend options and remaining decision

| Option | Concrete value | Fidelity/platform cost |
|---|---|---|
| Original PhysX 2.6.4 runtime | Best eventual comparison oracle for scene stepping, contacts and body behavior | Installed Windows x86 DLLs are not a native macOS ARM64 library. No usable 2.6.4 source/native build has been established. Existing 2.8.1 headers are neither the exact ABI nor an implementation. |
| Modern PhysX | Same middleware family; available source, native CPU simulation and cooking APIs | It is a new solver/API and cooking generation. Requires adapter and behavioral comparison. Current public build files inspected expose Windows/Linux, including Linux ARM64, without a macOS preset; native Mac integration is additional work. |
| Jolt | Explicit macOS x64/ARM64 support, CPU NEON/SSE-family implementations, raw triangle/convex inputs and a small rigid-body library boundary | Different solver, contact generation, material rules, constraints and sleeping behavior. Reproducing the original scene needs deliberate mappings and comparisons, not simply exchanging library names. |

Current NVIDIA source is pinned at
[`b97365c6ddb32fda8469bc788ec18024862b0368`](https://github.com/NVIDIA-Omniverse/PhysX/tree/b97365c6ddb32fda8469bc788ec18024862b0368).
Inspected `physx/source/compiler/cmake/CMakeLists.txt`, its platform directory,
`physx/buildtools/presets/public`, and `physx/documentation/platformreadme`.
The top-level CMake file requires the selected platform's CMakeLists; the public
tree has Linux and Windows implementations. Linux AArch64 does not establish
macOS ARM64 support.

Jolt platform and CPU support is from its primary
[README at `6f8bc72fd5fc6a58a52a7fbfe49ca7b5170b4990`](https://github.com/jrouwe/JoltPhysics/blob/6f8bc72fd5fc6a58a52a7fbfe49ca7b5170b4990/README.md).
This is a candidate assessment, not a Jolt implementation study or a completed
native backend build. Before adopting it, inspect the concrete body/query/step
implementation and prove the required native consumer. Console availability and
packaging are a separate target check; a desktop build does not establish them.

Recommendation: proceed with the recovered static character resolver and actor
animation now. Enumerate the required dynamic-body features and compare their
contract to modern PhysX and Jolt before selecting the provider. For this Mac,
Jolt is the practical native candidate; modern PhysX's name alone does not erase
its current platform adaptation or behavior differences. Neither candidate should
replace the recovered game movement policy.

## Bounded next implementation contract

1. Prepare immutable character collision geometry and its broad phase at scene
   publication. Retain ownership, authored flags and shape/object identity. Do
   not extend the camera's per-part scan into an unmeasured every-actor hot path.
2. Reconstruct the established sphere/capsule contact, segment, support and slope
   operations with explicit result types. Use instruction/numerical witnesses
   for status values and edge cases; queries allocate no per-frame scratch on
   the general heap. Fixed/bounded query scratch belongs to the query owner.
3. Port the game's movement states on that geometry contract. Publish resolved
   actor pose and support state before their animation/camera consumers. Retain
   explicit pose placement as teleport/spawn, separate from ordinary movement.
4. Keep dynamic scene mutation/step/result publication separate. Scene creation,
   allocator ownership, authored body/shape data, collision filters, material
   mapping and pending changes form the provider input. Body poses, contacts and
   query results are outputs; game actor transforms are not globally overwritten
   by a solver step. Retire the scene only after queries/results cease borrowing
   its resources.
5. Verify standing on ground, wall approach/sliding, corners, slopes/steps,
   leaving an edge, jump/head obstruction/landing and checkpoint placement on
   the actual authored geometry. Add moving-platform and prop interaction
   acceptance when that backend contract is implemented. A fixed actor clip
   preview does not satisfy traversal acceptance.

Still unresolved before claiming full traversal: exact player jump initiation
and remaining airborne/landing branches, complete query shape dispatch and tie order,
remaining surface/filter branches, moving-support response,
dynamic-body writeback and actor/prop impulses. These are narrow reconstruction
tasks, not a new feasibility gate or a reason to delay independent animation.

## Reproduction and evidence

Read-only Ghidra exports against the existing hash-bound PC analysis image:

- `research/evidence/engine/pass-67-physics-boundary/`: Runner handlers,
  registrations and runtime triangle cooking.
- `research/evidence/engine/pass-68-character-collision/`: shape constructors,
  candidate dispatch, support-state creation and collision helpers.
- `research/evidence/engine/pass-69-jump-boundary/`: jump entry, gravity and
  displacement instructions, airborne collision and nearby landing handlers.
- Earlier passes 31, 32, 38, 40 and 41: player constructor, original query and
  shape initialization, already used by the camera reconstruction.

Each export preserves command, seeds, pseudocode, instructions and references.
The tool is `python3 research/tools/engine_slice.py <pass-name> <hex-address>...`.
Pseudocode is inferred; it is cross-checked against instructions when parameter
types or floating-point/status representations affect the contract. No game
runtime comparison, new physics backend build or dynamic simulation was performed
for this research. Generated exports remain ignored and the game directory was
not modified.

### Shared static query implementation

The `scene/spatial/character_collision` owner prepares bounded
world-space triangles and analytic capsules (coincident axis endpoints represent
spheres). Placed boxes and convex hulls contribute their actual authored surfaces,
not model bounds. The immutable median-split hierarchy prepares once and uses
fixed local query scratch. Unreal GeometryCore `DistSegment3Triangle3`,
`DistLine3Triangle3`, `DistPoint3Triangle3` and `MeshAABBTree3::SplitTriSetMidpoint`
at the pinned revision above informed feature minimization and setup-time bounds.
A median partition is an explicit adaptation to guarantee bounded stack depth.

For analytic capsule segment intersection, inspected Jolt's
[`RayCapsule.h`](https://github.com/jrouwe/JoltPhysics/blob/6f8bc72fd5fc6a58a52a7fbfe49ca7b5170b4990/Jolt/Geometry/RayCapsule.h),
`RayCylinder.h` and `RaySphere.h` at the same pin: solve the radial quadratic,
qualify the finite axis, then test endpoint spheres. Our world-space dot-product
form avoids a temporary inverse transform. This is a geometry algorithm reference,
not adoption of Jolt's rigid-body solver. Triangle segment queries preserve
front-face entry; primitive entry queries likewise allow penetration correction
to move an already overlapping body outward.


### Dimensions, flags and numerical implementation boundary

The raw `witch/actor/pw0/body` record is the actor's +0x124c definition:
factory `0x45b526` obtains it, the final argument chain supplies it to `0x5fe070`,
and `0x5fe125` stores that argument. Offsets +0x24/+0x28/+0x2c are standing
height 1.7, alternate height 1.3 and radius 0.4. `0x604930` subtracts twice the
radius, giving hemisphere-center separation 0.9 or 0.5. `0x5f3870` builds its
axis from the actor position to position + that separation in Y: the position is
the lower hemisphere center, not the sole of the character's foot.

Player factory `0x5fe970` sets query exclusions at +0x1274 to 0x100. The attached
behind-camera object makes +0x1214 nonzero for ordinary player operation, avoiding
the extra 0x1000 exclusion. The +0x127c field accumulates airborne seconds;
`0x6026c0` adds 0x4000 and invokes a recovery branch after 3 seconds. It is not
an ordinary velocity threshold. Moving-support and this prolonged-airborne
recovery branch are not silently interpreted as regular floor contact.

The shared implementation preserves previous-motion guarding, two penetration
corrections, guarded correction displacement, the grounded query lift
`max(1/30-dt,0)*7.5`, the expanded lower sphere, 0.01 clearance and the recovered
45-degree/down-probe constants. It qualifies support before applying it and
returns explicit wall/ceiling/support state. Query candidate order is stable
original registration ordinal after depth/fraction; exact original equal-depth
shape-dispatch ordering remains unresolved. The bounded static adaptation selects
walkable lower-sphere contacts and retains a higher edge support over a lower
floor probe; it does not reproduce every mutable-object and actor-flag branch of
`0x6030d0`. Its slope/step policy therefore needs actual authored-stage checks and
later original-runtime comparison, not an assertion of bit-identical behavior.

Prepared world geometry uses a median BVH rather than the original spatial grid.
No SIMD promise is made: this is a scalar bounded narrow-phase implementation
behind the shared geometry boundary. Triangle/axis distance is feature-based,
not a per-frame full mesh scan. Target-specific batches remain possible after
measuring the established workload. Analytic spheres/capsules do not acquire
render-mesh tessellation error.

In-process public-contract checks exercise source-sized standing/landing,
ascending motion, walls, corners, upper-hemisphere ceiling contact, loss of floor,
a walkable ramp, repeated 0.2-unit step traversal, analytic placed capsule/sphere
contacts, camera primitive entry, hierarchy selection and domain retirement.
Build and fatal targeted clang-tidy evidence is retained under ignored
`research/evidence/reconstruction/traversal/`. Root integration owns the complete
check suite and actual authored-stage traversal; those are not claimed here
before they run.

### Authored-stage contact acceptance

A standalone read-only probe loads the actual `st04_c` definitions through the
same catalog and static-object registration/combined-file precedence as the
application. It prepares 2,150 registered parts and exercises the public contact
API against authored faces. The recorded run establishes:

- Building walls stop the capsule at its radius plus clearance, while a
  60-update tangential path advances six units along the wall.
- Authored ramp faces retain qualified support with their actual normals.
- Building overhangs at Y=2 stop the upper hemisphere during upward motion;
  they do not report grounded contact.
- Three selected low rises (approximately 0.186, 0.186 and 0.1 units) are crossed
  by repeated ordinary movement updates on gate/road geometry.
- Leaving a building platform loses support. A separate position outside the
  authored world receives no artificial floor.
- Scene/domain retirement returns to zero live bytes.

The candidate search also retained a blocked 0.2298-unit rise beside `fence01` /
`gate`. Nearby geometry, starting contact and the original controller policy
matter; a height difference alone is not proof a surface is intended to be
walkable. This is recorded rather than changing a threshold to force that case.
The contact classifier itself was corrected from direct source evidence:
`0x5f44e0` loads double `0x851078` = 0.8028514385223389 radians, calls cosine,
and stores float 0.6946584 at world+0xa8080 (46 degrees). The downward-probe
constant is independently 45 degrees. The implementation retains both.

Probe source, exact compile/link command, face/position/normal JSON lines and
retirement/timing log remain ignored under
`research/evidence/reconstruction/traversal/static-stage-*` and
`static_stage_probe.cpp`. Constant bytes and a bounded `llvm-objdump` instruction
witness are in `contact-threshold.json` and its instruction companion. The probe
is a reconstructed-engine geometry acceptance run, not execution of the original
Windows game and not a claim of dynamic rigid-body parity. Placed PHIT shapes
are tested separately when their prepared initial transforms are admitted.

Prepared camera queries now traverse the same immutable hierarchy while keeping
the original triangle/coplanar narrow phase and incidence-adjusted clearance.
They do not repeat the camera-only fallback's full part scan on every live tick.

The ordinary player branch also adds one world unit to grounded downward-probe
reach when its attached camera/controller pointer is nonnull. This is exposed as
`CharacterCollisionRequest::ground_probe_extension`, supplied by the game owner
as 1.0 for that branch. It does not increase upward step correction and is ignored
for airborne/ascending support. Public-contract tests distinguish default short
following from this explicit player policy and verify it cannot land a rising
jump early. The actual-stage probe uses that same player extension; its latest
witnesses retain all the results listed above.

Representative placed-shape acceptance now uses the real concept package's
`st04_c.bgb` matrices and production PHIT decoder/preparation: container instance
101 (135-degree yaw), barrel capsule instance 28 and convex drum instance 5.
The world box corners and capsule endpoints independently reproduce the composed
local/body transforms within 0.000007 world units; the selected convex support
plane bounds all source points. All three camera probes retain approximately
0.05 clearance. Character side contacts retain approximately 0.4100–0.4112
units from their source face/axis surface, matching the 0.4 body radius plus
clearance and adjacent-face correction. Retirement again reaches zero live bytes.

The source/pose/query records are in ignored
`research/evidence/reconstruction/stage/placed-shape-witness.{cpp,jsonl,log}`;
independent numeric checks are retained as
`research/evidence/reconstruction/traversal/placed-contact-verification.json`.
See [placed physics collision](placed-physics-collision.md) for the typed shape,
convex hull and frozen initial-pose contracts. These witnesses validate static
contacts against transformed authored shapes; they do not validate solver-driven
prop motion, impulses or destruction.

### Floor-alignment review, reopened

A later user review reports feet/legs sinking through visible terrain, rather
than the complete capsule falling through the map. Independent sampling of the
recorded native checkpoint trajectories reconstructs the supporting authored
triangle at each actor X/Z and compares sphere-plane distance, not just the
returned grounded flag. The sampled minimum capsule-plane clearance is
0.009993 world units. At checkpoint 0 idle, center Y is 0.596971 and the
`hi_doukutu` face 1 plane is Y=0.185391 with normal Y=0.996172; the radius-0.4
sphere retains 0.010005 clearance. After the recorded jump, center Y=0.641498
and floor Y=0.229918 likewise retain 0.010005 clearance. Checkpoints 10/20
stand at center Y=0.410004 over collision Y=0.000004.

The render mesh and collision mesh are distinct authored surfaces. At the
checkpoint 0 center, static render ground is Y=0.121697
(`st04_c_doukutu`, draw 159, face 10), below the collision surface. The model's
source local offset of -0.5 places its origin at Y=0.096971. This is an origin
comparison, not the location of the skinned sole: changing collider radius or
adding a visual lift from it alone would be unjustified. The actor/animation
owner must compare actual skinned vertices at their own X/Z across idle, walk,
takeoff, flight and landing. These recorded samples do not reproduce every
position in the user's latest session and cannot close the reported defect.

The independent read-only probe and source face coordinates are retained as
`research/evidence/reconstruction/traversal/floor_alignment_probe.cpp`,
`floor-alignment-command.json`, `floor-alignment-input.txt` and
`floor-alignment.jsonl`. Its input is the prior `native-traversal-final.log`;
it does not claim a fresh GUI trajectory. The probe also decodes static render
triangles only for comparison; they are never supplied to production collision.

The conditional radius doubling in both original resolvers is a health-state
branch, not an ordinary player origin adjustment. `0x602b70` consumes the same
vtable +0x28 getter with +0x2c as current/max HP for HUD presentation, and its
nonpositive result enters death handling; `0x5fe970` initializes current HP to
100. The living player therefore retains radius 0.4 and cylinder separation 0.9.

### Source correction: model origin follows body radius

The common constructor's -0.5 model offset is only an initial value. Original
actor update `0x602b70` overwrites it every update with the negative effective
collision radius. Instructions `0x602d58–0x602dbc` load the body definition at
actor +0x124c, read radius +0x2c, choose the health-dependent multiplier, load
the model at actor +0x1210, multiply the radius, XOR its sign bit and write
model +0xd4. For living Alicia this is **-0.4**, not -0.5. The reconstruction
retained the constructor value and therefore placed the mesh 0.1 world unit
too low. The actor owner must derive this offset from the same body settings;
the collision capsule, floor geometry and gravity require no compensating bias.

The existing hash-bound pass 77 export contains both the pseudocode and exact
instructions in `functions/00602b70.c` and `instructions/00602b70.txt`.
An independent costume-5 skin sample supplies the following measurements at
checkpoint 0, using actual vertex X/Z to query the corresponding render face:

| Sample | Skinned minimum Y with old offset | Render floor Y | Old penetration |
| --- | ---: | ---: | ---: |
| Stand | 0.111818 | 0.139718 | 0.027900 |
| Generic jump start, 0.1 s | 0.102233 | 0.152736 | 0.050503 |
| Generic jump start, 0.2 s | 0.113157 | 0.140008 | 0.026851 |

These samples hold the body at center Y=0.596971 and do not represent a moving
jump trajectory. Applying the recovered radius-dependent offset raises those
vertices by exactly 0.1; the actor owner verifies the completed animation and
movement path. The stand sole then lies at Y=0.211818, near its authored
collision surface Y=0.214680, while the detailed render surface is lower. Do
not erase that authored render/collision difference with a per-scene offset.
Skin values are retained in `skinned-foot.log`; independent floor queries are
`floor-foot-input.txt` and `floor-foot.jsonl` in the traversal evidence directory.
Their search uses body Y at foot X/Z, so their convenience capsule-gap fields
are not actual capsule-center measurements; the earlier trajectory report is.

## Explicit grounded contact mode

The stationary raised-turn review exposed a reconstruction defect: a small
positive authored root Y displacement was passed as vertical velocity, and the
shared resolver suppressed floor support as though Alicia had begun a ballistic
jump. The next state became the generic falling animation despite remaining
above the same floor. At the first failing frame349, previous and proposed
world positions were identical, root Y was `4.43987751e-16`, and derived vertical
velocity was `2.66392641e-14`. The movement state was grounded, but its floor
result was false. This is a state-policy error; no numerical epsilon is added.

Original `6030d0` always performs its small-tick body lift and downward support
work after gathering candidates; it has no vertical-velocity sign gate.
`5ff410` uses this path for ordinary root/contact, scripted start `6649f0`
uses it without root movement, scripted landing `664900` uses `5ff410`, and
Dodge landing `68a370/68a4d0` calls it directly. Generic flight `6644b0` and
Dodge takeoff `68a720` use the separate `6026c0` path.

`CharacterContactMode` now records that state-owned distinction explicitly.
Grounded queries retain support for positive animation displacement; airborne
queries retain the current ascent exclusion. Previous contact controls continuity
reach, not movement mode. The existing geometry and source radius/tolerance
remain unchanged. This follows the already inspected pinned Unreal
`PhysWalking`/`PhysFalling` separation: walking explicitly updates floor after
movement, while falling owns its separate contact/landing policy. Unreal's
constants and controller algorithm are not substituted here.

The airborne ascent exclusion remains a reconstruction policy, not a claim that
`6026c0` branches on velocity: its exported instructions do not read the first
stack argument passed by the flight caller. This correction does not broaden
that separate fidelity question. Verification pairs a positive grounded root
rise with an ascending airborne query over the same floor, and retains wall,
ceiling, descending landing, step, slope and support-loss checks. The native
stationary raised/pitch/turn/fire sequence is owned by the integration verification; these unit checks alone do
not establish that native review result.
