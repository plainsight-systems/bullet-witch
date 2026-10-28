# First combat encounter: physics prerequisite

October 27, 2026. Research following accepted mission startup `772f4a4`.
The user approved the Jolt recommendation; the bounded provider foundation is
implemented below. The original skeletal recipe importer and provider joint
contracts are implemented; integrated skeletal handoff remains the game owner’s work.
Parent map: [combat physics and effects](combat-physics-effects-plan.md).

## Finding and recommendation

**The complete first encounter needs dynamic bodies.** The original common death
state first plays an authored death animation, then hands the body to physics.
Holding the final animation pose indefinitely would omit source behavior. This
also applies to Alicia's death, so avoiding a particular enemy death variant
does not remove the requirement from the agreed kill-or-be-killed acceptance.

**Jolt is approved behind a world-owned provider boundary for the native port.**
A bounded ARM64 executable has exercised the required kinds of integration:
raw triangle cooking, filtered segment queries with body identity, a kinematic
body becoming dynamic, off-center impulse, constrained bodies, synchronous
advancement, pause by withholding advancement, and clean removal. This is a
provider feasibility witness, not imported Bullet Witch ragdoll validation.

Current upstream PhysX is a viable Windows/Linux physics library, but its public
5.12 release does not support this Mac build out of the box. Choosing it requires
owning an additional Apple build/platform port. PhysX 5 is also not the game's
PhysX 2.6.4 solver; retaining the product name does not establish numerical
fidelity. The approved production dependency is built from pinned source.

## Original encounter and terminal behavior

The recovered One Shot Kill character table starts with `ea0` rows and also
contains `ea1`; production `WorldPopulation` maps `ea0/ea1/ea2` to shared `ea0`
motion ownership. These are compatibility names, not a reason to use a different
enemy family or a generic replacement controller. Actual activation continues
to come from the accepted mission triggers.

Original unpacked image hash:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Read-only exports used here are the earlier passes 321–326 and the bounded
`pass-328-first-combat-body-handoff` export. Instructions and installed data
take precedence over the decompiler's frequently incorrect float/register types.

| Source path | Recovered obligation |
|---|---|
| `6867b0 -> 6879f0` | Filter incoming receipt, use body-indexed coefficients, mutate current/recoverable health through Damager policy, then choose survival or death. |
| `6867b0 -> 687ad0 -> 687ff0/6881a0` | Direction, hit category, magnitude and authored weights choose a death/reaction identity. Do not select an arbitrary clip based only on zero health. |
| `5881d0 -> 5a54a0` | Creates retained `StateDie`; RTTI at `97ce00` verifies this identification. Shared runner dispatch ultimately reaches `6839d0/683d40`. |
| `6839d0` | Explicit selected death motion starts at zero with fresh state and blend argument 0.3. Already-physical cases instead request body activation/impulse. |
| `683d40` | With ordinary actor flag `0x100` absent, completed or repeating motion requests physical ownership: clears selected motion, writes physical request `+7af8 = 1`, calls `4f6920`. Loss of support can also cause the handoff. Later root placement follows physical model state. |
| `4f6920 -> 4f7100` | If physics is not yet active, retain the impulse request. The pending record preserves priority, selected body, vector and application position; lower priority loses, equal priority keeps the greater squared magnitude. This is not accumulation of every request. |
| `4f6920 -> 4f4a70 -> 4f3b00` | Once active, select an explicit body index or a geometric fallback and queue a body operation. The native queue packs the operation mode with vector and position. Do not mutate the middleware during hit iteration. |
| `4f6b60` | Fallback body selection excludes bodies with tested flag `0x80`, obtains each eligible body position, and minimizes distance to the source-defined segment. It is not simply the nearest actor root. |

The special high-impact `66e070` branch is additional policy, not evidence that
ordinary animation death can bypass physics. Actor flag `0x100` is an original
exception; setting it on our actors to avoid implementing bodies is not faithful.
The common death receiver also retains effects/player-death work and delayed
retirement. A body is not destroyed merely because health reached zero.

## Queries, identity and damage stay separate

Machine-gun dispatch zero is a persistent straight projectile with source speed
240 and range 72. Keep its recovered movement integration and swept old-to-new
queries in the game projectile owner; do not ask the solver to integrate a
second physical bullet with invented gravity.

`61b1d0` first obtains scenery obstruction and then actor segment/radius results.
The scenery endpoint constrains the eligible actor query. `5f5cb0` queries the
source-selected candidate bank, returns actor identity, surface/body metadata,
position and normal, and shortens the endpoint to the accepted hit. The returned
packed shape value is split into low-byte body index and higher surface value.
`61b1d0` validates the actor generation before publishing its receipt, applies
source range falloff, and sends the concrete material impact through `61b8e0`.
The scenery path distinguishes damageable/callback bodies using `0x4000/0x8800`.

The provider therefore supplies geometry and stable body/subshape identity.
Game code owns side/source eligibility, body damage coefficients, invulnerability,
reaction and health, source callback flags, and material-effect selection. A
middleware object layer is not a replacement for the game's complete filter
matrix. Exact candidate-bank membership and all receiver policy branches remain
implementation contracts; this report does not label them reconstructed merely
because a ray API exists.

## Body description required before production integration

There are two distinct source paths. The current `PhysicsCollisionDefinition`
validates placed-object PHIT pointer graphs and body/joint mappings;
`PreparedPropCollision` converts their initial shapes for static queries.
Character dynamic bodies instead come from a native skeletal recipe table.
The earlier assumption that the first actors would supply PHIT was incorrect:
stable and One Shot Kill catalogs contain no `model.ea0.phit`/Alicia equivalent.

`4f4780` selects a model prefix through `4f6180` and table `00964d30`, retaining
its recipe at model `+7b00` and resolving named bones. EA0/EA1/EA2 share recipe
`00964cd0`; PW uses `00964cb8`. Both specify fifteen bodies and fourteen joints,
with hips, spine, head, three segments per arm and three per leg. `4f5600`
passes recipe body records to `4f4c80` and joint records to `4f5390`. These
source-authored values must be decoded; they must not be substituted with a
generic anatomical generator. The ignored `character-recipes.json` records the
hash-bound executable table census. Placed PHIT remains independently useful
for level objects; its descriptor mass/damping conventions are not evidence
of the character recipe layout.

The first encounter needs the actual `ea0` and Alicia body definitions to produce:

- Shape dimensions and local poses; body-to-bone mappings and body/world poses.
- Mass, inertia, center of mass, damping and original material parameters.
- Joint type, both local frames, angular/linear limits and connected-body filters.
- Animation-to-physical transition, initial velocities, source impulse mode and
  point, followed by physical transforms back to the skeletal presentation.
- Body/query membership and hit identity throughout death and deferred removal.

Raw triangle and convex cooking routes are established (`4f7ac0/4f79f0` and
`4ee1e0/4f7920`), so those paths do not require reading legacy cooked PhysX blobs.
That does not prove every PHIT shape or joint field is already understood.
Do not fabricate mass/limits, generate a generic anatomical ragdoll, or freeze
unsupported bodies to make an acceptance path look complete.

## Provider comparison and concrete source inspected

Pinned primary checkouts are ignored under
`research/evidence/reconstruction/first-combat-physics/`; `provenance.json`
records revisions and inspected source hashes.

| Boundary | Jolt | Current PhysX |
|---|---|---|
| Pin | `5d5275c38074c44b14f38b85bc9c73573078c808` | `b97365c6ddb32fda8469bc788ec18024862b0368`, SDK 5.12 |
| Native target | README/build documentation explicitly support macOS ARM64; current native probe built and ran. | CHANGELOG runtime support is Windows/Linux; public presets and platform CMake directories contain those platforms only. Apple preprocessor branches remain, which is insufficient build support. |
| License inspected | Library MIT; HelloWorld reference CC0. | `physx/LICENSE.md` and SDK README say Apache-2.0 at this pin. Top-level repository also contains a BSD license covering other content; do not assume one license covers every subtree. |
| Shape/body setup | `MeshShapeSettings`, capsule/convex/compound shapes, `BodyCreationSettings`, explicit mass/inertia override. | Raw cooking APIs, `PxShape`, `PxRigidDynamic`, mass-space inertia and center-of-mass setters. |
| Query identity/filter | `NarrowPhaseQuery::CastRay/CastShape`, body/object/shape filters, `BodyID`, `SubShapeID`, user data. | `PxScene` queries, pre/post filtering, shape/actor user data and hit face index. |
| Physical handoff | `BodyInterface::SetMotionType`, pose/velocity and impulse methods; `Ragdoll` has pose/kinematic-drive and result interfaces. | `eKINEMATIC`, `setKinematicTarget`, physical mode, force/impulse APIs and joint constraints. |
| Advance | Synchronous `PhysicsSystem::Update` with injected temporary allocator/job system. | `simulate/fetchResults` and dispatcher/allocator callbacks. |
| Fidelity limit | Different solver from 2.6.4. Authored contracts must be translated and validated. | Different major-generation solver and API from 2.6.4. Same requirement. |
| Future console | Public documentation does not establish Xbox Series X/GDK support. | This public SDK platform list does not establish Series X/GDK support either. |

Primary pinned sources:
[Jolt build/target documentation](https://github.com/jrouwe/JoltPhysics/blob/5d5275c38074c44b14f38b85bc9c73573078c808/Build/README.md),
[Jolt body interface](https://github.com/jrouwe/JoltPhysics/blob/5d5275c38074c44b14f38b85bc9c73573078c808/Jolt/Physics/Body/BodyInterface.h),
[Jolt ragdoll boundary](https://github.com/jrouwe/JoltPhysics/blob/5d5275c38074c44b14f38b85bc9c73573078c808/Jolt/Physics/Ragdoll/Ragdoll.h),
[PhysX supported release platforms](https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/CHANGELOG.md),
[PhysX platform build dispatch](https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/source/compiler/cmake/CMakeLists.txt),
[PhysX body API](https://github.com/NVIDIA-Omniverse/PhysX/blob/b97365c6ddb32fda8469bc788ec18024862b0368/physx/include/PxRigidBody.h).

The comparison does not warrant a third provider at this stage. Jolt meets the
native feasibility requirement directly; pursuing other libraries would not
recover the missing original descriptors or prove old-solver parity. An actual
Xbox build remains a target-specific qualification task, not a promise based
on a Windows compiler or an unnamed console in a README.

## Ownership to preserve

The original world owns its physics service and advances it after queued
mutations. `4fc050` configures timing with `min(dt,1/15),4,0`, then simulates the
**original delta** and blocks for results. These arguments are not permission to
drop elapsed time or install another clock. Modern provider subdivision must be
an explicit adapter decision, not assumed equivalent to historical Nx timing.

The engine world owns provider scene/body/constraint lifetimes. Expose read-only
queries, bounded mutation submission, and a separate world-only advancement
capability. Do not expose `PhysicsSystem` or `BodyInterface` directly to gameplay.
Commands copy arguments and validate generation at execution; observations copy
results after the completed step. Renderer and HUD observe outcomes. Actor
animation stops driving the bones that physical ownership has acquired.

Pinned Unreal `f45729c0b6f097038e72b52d366a62baeca3275b`, inspected again through
the saved `BodyInstance.cpp` and `ProjectileMovementComponent.cpp`, supports this
boundary: finite pose checks and scoped physics writes; separate kinematic move
versus teleport; dynamic-only impulses capturing value arguments; projectile
movement refusing to integrate an already simulated component. These guide the
unresolved interface, not Bullet Witch's numeric body values or damage rules.

Allocator callbacks must route through the existing engine memory owner, with
bounded scratch storage and documented global callback lifetime. The probe uses
Jolt's explicit default allocator bootstrap only to test feasibility; production
allocator integration is covered by the production foundation checks below. Do not create a second general
job pool by copying HelloWorld. A serial job adapter or the engine's eventual
scheduler must retain sole world sequencing and blocking completion.

## Native probe and limits

The ignored `provider_probe.cpp` reuses the primary CC0 HelloWorld layer-filter
example, adds a raw two-triangle floor and three capsule bodies, then exercises:

1. A segment excluding its shooter hits the intended capsule, preserving user
   identity (fraction 0.485).
2. That capsule switches from kinematic to dynamic and receives an off-center
   impulse; a constrained body pair advances for 240 intervals of 1/60 second.
3. The pair remains finite and supported; final constraint distance is 0.999999
   for requested 1.0. No clock advances when `Update` is withheld.
4. Constraint removal precedes body removal/destruction; zero bodies remain,
   and a new query finds no stale hit.

AppleClang 17 built the upstream static library and probe for ARM64. Configuration,
build output, executable source and JSON results are saved beside provenance.
The library build enabled its upstream compute capability definitions, but this
probe calls only CPU `PhysicsSystem::Update`; no GPU simulation was invoked.
Production should disable unused compute/render/tooling features explicitly.

This validates a real native provider, not a checklist or mocked interface.
It does **not** validate imported game constraints, exact 2.6.4 contact behavior,
cross-platform determinism, engine allocator integration, Xbox, real encounter
frame time or a visible ragdoll. The current upstream PhysX build was inspected,
not compiled or patched on macOS. No game was launched by this investigation. The user approved Jolt; typed
original-body translation remains required before dynamic gameplay is complete.


## Approved provider foundation

The user selected Jolt. `cmake/Jolt.cmake` builds immutable revision
`5d5275c38074c44b14f38b85bc9c73573078c808` from the source archive, SHA-256
`fbb4911723a4fe49c5b51281ef4ba10cffd71952d29afd4d8f0ffa9b3bd51f29`.
A normal FetchContent configure successfully downloaded/verified it into
`build/physics-provider/_deps/bw_jolt-src`. No prebuilt physics SDK is linked.
Compute, debug rendering, profiling and object-stream components are disabled.
RTTI and C++ exceptions match the engine: the disabled-RTTI trial caused an
actual-provider exception rejection test to abort instead of matching its
handler on AppleClang; the matched configuration passes that test.

`include/bw/physics/world.hpp` contains only engine types. Jolt types and compile
definitions are confined to `src/engine/physics/jolt`; downstream test compilation
has neither Jolt include paths nor JPH definitions. Runtime installs one allocator
registration and rejects a competing live registration. Every world retains it;
weak query/mutation capabilities do not prolong world lifetime. Provider heap,
shape cooking, bounded command/slot buffers and scratch allocations route through
the explicitly retained engine CPU domain. Upstream OOM terminates rather than
returning an unchecked null; engine capacity/descriptor errors reject explicitly.

The world is serial on its creating thread. It consumes its issuing immutable
WorldFrame once, applies value commands before synchronous update, and skips both
commands and simulation during pause. The single-threaded upstream job adapter
runs its job graph without creating a second engine thread pool. Frame delta is
not capped or independently accumulated. Queued motion state permits an explicit
kinematic-to-dynamic handoff followed by an off-center impulse in one interval.
The custom Alicia controller and game health/projectile rules remain outside.

Resource identities include world, slot and generation. Queries return copied
body/shape identity, user receiver payload, material/surface, point, fraction and
geometric normal. Mesh triangle surfaces survive cooking. Constraints must retire
before bodies, and bodies before their retained shapes. The initial explicit
constraints include distance for foundation verification and source-required
hinge/cone-twist joints. These are provider algorithms, not an anatomical
substitute for the game's authored body and joint tables. Local placed shapes
retain their child identities. Hinge/cone joints suppress connected-body
collisions through a world-owned symmetric pair bit table; the final retiring
joint restores that pair. This table occupies N*(N-1)/2 bits at initialization.

Focused `memory-tests` passes with actual Jolt bodies: layer/excluded-body queries,
triangle surface identity, handoff/impulse, pause, foreign/repeated frame rejection,
mesh contact and constrained motion, ordered retirement, stale/foreign IDs,
expired capabilities, competing registration rejection, and zero engine-domain
allocations after complete teardown and re-registration. Fatal scoped clang-tidy
passes the provider implementation and test translation units. Integrated full
repository checks and independent review are owned by the parent slice. This
foundation establishes no claim of visible game ragdoll parity.


## Native character recipe importer and joint construction

`src/game/content/original/character_physics_recipe.hpp/.cpp` reads only data
from the purchased `witch.exe`. It checks PE32 section bounds and image base,
resolves the sixteen-entry native prefix table case-insensitively, and returns
copied fixed-capacity body/joint records. Its sixteen-slot capacity follows the
native model owner; no borrowed executable pointer escapes. The module never
runs native code and does not consume decompiler output or a generated JSON
fixture. The original file hash in the local validation is
`f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc`.

The ignored `recipe_census.py` compares every body/joint/bone-pointer region
against the separately hash-bound analysis image. Fifteen non-null prefix
recipes match byte for byte in the purchased original. `recipe_probe.cpp`
executes the production importer against that original file: PW, EA0 and EA1
all yield fifteen bodies and fourteen joints. This is file/import verification,
not proof of a rendered ragdoll. Synthetic unit coverage separately exercises
section bounds, copied lifetime, source length/radius conversion, case-insensitive
prefixes, missing definitions and invalid joint topology.

`pass-329-character-physics-recipe` preserves both instructions and decompiled
helpers; `pass-330-combat-source-adapters/004fbb20` closes box shape construction.

| Original owner/helper | Reconstructed contract |
|---|---|
| Recipe +0/+4/+8/+12/+16/+20 | Name, bone-name array, body count, joint count, body records, joint records. |
| Body record, stride 32 | Kind, mass, geometry float3, local offset float3. Values are read from the executable, not reproduced as a C++ table. |
| `4f4c80` | Kind zero is sphere. Kind one is box half extents. Kind two is also a box: half extents `(length/2, radius, radius)`, not a capsule. Body construction starts from the named bone’s authored bind hierarchy. |
| `4f4ee0/4f5a00` | Shape local translation uses the authored offset; boxes add half extent X to its X. Supplied mass requests shape-derived inertia. Starts kinematic with linear damping .3 and angular damping .5. |
| `4fbb20` | Copies box extents unchanged and writes skin width .01; skin is not an instruction to inflate geometry. |
| Joint record, stride 32 | Kind, first body, second body, group, axis float4 with direction W zero. |
| `4f5390` | Bind-pose second-body origin is the anchor. First-body bind rotation transforms the authored direction into that construction space. Native global-axis/global-anchor helpers derive each body's local frames. |
| `4f5750` | Hinge; group two gets ±30 degrees, other groups zero through 150 degrees. Connected-body collisions are disabled. |
| `4f5ec0` | Spherical; twist ±40 degrees, circular swing cone 60 degrees; twist spring stiffness .5/damping20/target0; swing spring stiffness2/damping40/target0. Limit restitution/hardness .5; flags enable both limits and both springs. |

The spring interpretation checks instruction stack offsets against the historical
`NxSpringDesc` field order: **spring, damper, targetValue**. These are physical
coefficients, not guessed frequencies. The ignored historical headers are
PhysX 2.8.1 redistributions and corroborate field semantics; they are not claimed
to be an exact 2.6.4 binary SDK. The actual binary initialization is the authority.

The engine adapter uses pinned Jolt `RotatedTranslatedShapeSettings`,
`HingeConstraintSettings`, `SwingTwistConstraintSettings` and `GroupFilterTable`.
Frames are explicit world-space orthonormal values. Cone/twist springs use
`ESpringMode::StiffnessAndDamping` and position motors at identity constraint
orientation. Joints can be created while both bodies are kinematic; each still
has movable mass for the later handoff. Kinematic targets use Jolt
`MoveKinematic` over the issuing world delta; reset poses use explicit teleport.
A paused or zero-delta frame does not consume queued targets. Speed ceilings
are explicit and unrestricted by default, avoiding Jolt's unrelated default
velocity caps on source impulses.

### Skeletal authority and provider differences

`4f6d10` evaluates each named bone's accumulated animation transform and queues
its body target; reset uses the separate pose operation. `4f6dd0` performs the
reverse conversion after physical ownership: obtain each body global transform,
resolve the skeletal parent's accumulated transform, invert it, and compose
local bone rotation. Non-root bones retain their authored local translations;
the physical pelvis transform is promoted to the model root and the pelvis
local transform becomes identity. The skinning palette must be rebuilt from
those resulting local poses. Writing body world matrices directly into skinning
palette entries would bypass the original hierarchy and is not supported.

Jolt is an approved replacement solver, not PhysX numerical equivalence.
Original limit restitution/hardness do not have direct corresponding Jolt
hinge/cone parameters. Original separate sleep linear/angular thresholds
(.15/.14) and PhysX skin-width contact generation also do not translate one for
one to Jolt. The importer retains the source limit/skin values; provider
constraints use Jolt's established solve/contact behavior rather than a second
handwritten solver. Visual ragdoll acceptance must assess these declared
adaptations. No claim of identical collision settling, bounce or sleep timing
is made.

The focused provider suite additionally covers transformed-shape retention,
an animation target held during pause then admitted, rejection of kinematic
writes after physical handoff, hinge attachment through handoff, and cone/twist
spring advancement and retirement. Full encounter ownership, source impulse receipt selection and native visual
acceptance belong to the integrating game owner; these tests do not close those
boundaries by themselves.


## Character instance adapter

`src/game/actors/character_physics.hpp/.cpp` owns the per-instance source recipe
resources. It takes separate weak `Resources`, `Queries` and `Mutations`
capabilities; none can advance physics or mutate the world clock. Initialization
resolves all named bones and a parent-first hierarchy once. Failure rolls back
created joints, bodies and shapes. Retirement releases joints before bodies,
local shape wrappers before primitives. An already-expired world has performed
its own complete teardown.

The bind/current distinction matters: `4f27f30` composes authored bind parents;
`4f5600` creates bodies and joint frames in that space, then places the bodies at
the currently evaluated animation transforms. The adapter obtains equivalent
local frames by mapping each bind-space joint frame independently into its
current body pose before provider construction. It does not treat a spawn
animation as the joint's zero-angle pose. The actual-provider unit test starts
a limb beyond its authored hinge limit and verifies convergence toward the bind
limit, which would fail if the initial animation became its rest frame.

`follow_animation` validates all target poses and queued-command capacity before
publishing the batch. `handoff` queues dynamic authority once. Impulse selection
remains game death policy; the adapter accepts an explicit body index and world
application point. `publish` requires the world to have admitted that handoff,
then performs the original local rotation/authored translation/root promotion
steps above and calls the existing `SkeletonPose::evaluate_locals`. It does not
advance animation, perform live EHIT damage queries or choose a death clip.

Matrix/quaternion conversion lives in shared `bw/math/rigid_transform.hpp`,
with the trace/largest-diagonal algorithm from pinned Jolt
[`Mat44::GetQuaternion`](https://github.com/jrouwe/JoltPhysics/blob/5d5275c38074c44b14f38b85bc9c73573078c808/Jolt/Math/Mat44.inl).
The row-vector convention is verified across identity, half-turns on each axis,
and a mixed-axis rotation. Nonrigid scale/shear/reflection inputs reject; small
orthonormal floating-point drift is validated before quaternion normalization.
There is no local controller matrix inverse or second math library.

Focused actual-provider tests cover kinematic follow, source bind joint frames,
dynamic impulse, root promotion, retained child bone translation, ordered
retirement, preparation rollback and zero retained domain allocations. Both
existing `runtime-tests` and `memory-tests` pass. These are in-process subsystem
checks, not a claim that EA/Alicia's full native encounter has been accepted.

The original scene constructor `4fb2c0` (`pass-06d`) initializes gravity from
`9658e8/ec/f0` to `(0, -9.8100004196167, 0)`, maximum timestep 1/60 and iteration
limit eight. These are source scene settings, distinct from `4e9660`, which only
forwards the world timestep and is not proof of a gravity default. The
world composition owner selects the provider's stepping/material policy using
this evidence; the character adapter does not silently install scene defaults.

### Scene geometry, materials and provider admission

`WorldPhysics` owns the provider registration and scene, consumes the issuing
world frame and lends separate query, resource and queued-mutation capabilities.
It prepares geometry from `StaticCollision`, without decoding a second copy of
HIT vertices or borrowing rendered triangles. The collision owner now preserves
bank/category metadata and physics-only records alongside its existing immutable
query hierarchy. Existing player/camera query admission and algorithms remain.

This distinction follows `5f4ea0 -> 4e95a0 -> 4e7900`, not an assumption that every
camera surface belongs to PhysX. Combined HIT geometry is cooked. For separate
stage banks, CHIT ordinary or special-category parts reach physics; solid-category
`0x40` parts do not. Character-query exclusions `pp_`/`hp_` can still have physical
geometry. Physics registration rejects `wa_`, `de_`, `pw_`, `pc_`, `np_`, `nc_`
and `ne_`; `cc_` remains eligible. EHIT contributes the environment query bank,
not another duplicate physical terrain. Physics-only parts never enter the
accepted character/camera query traversal or the projectile/effect scenery bank.

Descriptor interpretation was checked against the instructions, because the
initial decompiler view could be misread: `4f7ac0` descriptor word +24 is the
16-bit-index flag (`2`), while material-index stride/pointer remain zero. Its
`0xff`, or `1` for `hi_`/`hp_`, is the heightfield vertical axis, **not** a material
index. The mesh shape created by `4e7900` uses material zero: the dword at
shape +3c is one, meaning collision group one and high-half material zero. Triangle byte seven
remains the game surface classification, separate from physics material. The
heightfield thickness/axis acceleration semantics are not reproduced by the
ordinary Jolt triangle mesh; this remains a documented provider adaptation.

`4fbf80` instruction export (`pass-06e`) establishes material zero as restitution
`.2`, static friction `.7`, dynamic friction `.5`. It constructs thirteen
materials from native table `9656f0`, stride 20: index, combine mode, restitution,
static friction, dynamic friction. Both coefficient-combination fields receive
the table mode. `read_scene_physics_recipe` copies these records and source
gravity from purchased `witch.exe`, using the same checked PE reader as character
recipes. No native table dump or proprietary generated fixture is embedded.
The historical redistributed NxMaterialDesc header corroborates field meanings;
it remains a 2.8.1 reference, not proof of an exact 2.6.4 ABI.

The provider supports average/minimum/multiply/maximum coefficient combination;
the larger mode wins, as in the original material contract. Copied mode bits
live in private body metadata, with no game callback/global world lookup.
Jolt has one Coulomb friction coefficient, so this adapter selects the authored
dynamic friction and retains static friction in the imported record. It does
not implement a second friction solver or claim exact PhysX contact parity.
Connected-body filter changes invalidate Jolt's contact cache immediately.

Layers zero through two are query-only character, environment and combined
scenery. Layer three is physical terrain, four physical character bodies;
actor EHIT layers start at five. Game receiver metadata is copied and keyed by
world/generation-aware body identity. Source bank-one effect/projectile queries
include environment and combined scenery, never physical-only terrain. Frozen
PHIT-derived props are not mislabeled stage meshes: their real query registration
and dynamic object owner remain a separate source admission path.

Receiver kind is constructor-proven, not inferred from triangle geometry:
`44aa90` initializes the 8,192 raw stage records at world +8020 with 0x50 stride
and kind **4** (`pass-335`). `548400` initializes transformed mesh kind **5** and
its matrix-bearing representation (`pass-334`). Effect42 therefore uses its
original fixed collision axis on raw stage records; only transformed kind5 takes
the geometric-normal path. The scene metadata preserves this distinction.
Generic copied body query flags are tested before closest-hit selection; filtering
an already-selected hit would incorrectly discard farther eligible geometry.

The placed PHIT body constructor `4eedb0` (`pass-78`) provides another concrete
boundary for later admission. Every shape kind writes material index from the
source shape's uint16 at +8 **plus three**; instructions place it at SDK shape
+3e. Group is three if body descriptor +8 and dynamics pointer +16 are nonzero,
otherwise four. Missing dynamics requests kinematic body flags 0x180 and mass
1,000,000; present dynamics supplies mass/damping and flags OR 0x100. The frozen
traversal geometry does not retain all this owner/shape metadata, so it must not
be repurposed as source-correct projectile/damage receivers.

Latest focused verification: rebuilt existing `runtime-tests` and `memory-tests`
with the source-built provider; both pass. World tests cover physical-only
terrain, query-only ceilings, raw-stage receiver type, source bank/surface
metadata, whole-receiver and object-flag exclusion before closest-hit selection,
pause and full domain cleanup. The production importer probe reads the purchased
executable's gravity and all fourteen material slots, plus PW/EA0/EA1 recipes.
Fatal scoped clang-tidy passes all eleven affected translation units; formatting
and whitespace checks pass. Full repository gates and integrated encounter/native
acceptance remain the parent integration task; no visible game launch, commit or
push was performed by this workstream.

## Ordinary shot assembly and orientation

The combat adapter now separates camera aim queries, muzzle placement, target and
scenery reference. `62ed80` starts the camera ray at camera position plus forward
scaled by actor depth plus 0.1; its endpoint remains camera position plus weapon
range. The scenery and actor banks shorten that endpoint before shot submission.
`600de0` uses the visual record's projectile-origin bone at +0x10, distinct from
the muzzle-effect bone at +0x14. `630400` retains either that original muzzle or
the selected aim hit as the projectile's scenery reference. `616530` applies the
source lateral radius and forward-depth placement operations without moving that
reference. These are game adapters over shared vector/quaternion operations.

Enemy event-driven emission is a different caller: `698080` / `698a40` obtain the
pending authored animation-event bone through `699140`, compose its world pose,
and use its **negative Z** direction. This does not authorize substituting camera
forward or the weapon's effect bone for the event's bone. The ordinary native
Shooter message path (`62ea90`) is a distinct implemented producer: its
untargeted fallback adds the already-composed locator positive Z axis. The
separate animation-event negative-Z rule is not that helper's contract.

`615600` initializes spread from specification +0x78. `617a30` advances between
+0x78/+0x7c using widening/recovery durations +0x80/+0x84. The widening flag is
set by locomotion state (`602b70`, instruction range `602bee`–`602c71`), not by
shot acceptance. `616530` preserves negative cooldown carry as the first shot's
initial age; additional emitted shots decrease age by the firing interval.
Its separate recoil RNG draw precedes even a cooldown rejection. The cone helper
uses two shared session draws, including at zero spread, with uniform polar
angle as recovered from `5df950`; it does not replace this with uniform solid
angle sampling.

The source orientation helper `409b70` has explicit singularity handling. Exactly
collinear up/forward vectors choose alternate up based on forward Y; a very
short right vector uses the source default right. The reusable math helper takes
that policy as a value, and the game adapter supplies the original constants.
This leaves existing unrelated basis callers unchanged. Vertical impact normals
and downward shot cones are supported; they are not rejected as exceptional
geometry. The opposite-vector quaternion axis follows `407c90`'s basis-derived
choice.

Focused evidence lives under ignored `first-combat-physics`: executing original
instructions for 36 cone cases matches production directions within 1.20e-7 and
preserves exact random state; nine basis cases, including both vertical axes and
near-collinearity, match within 5.97e-8. Only standard libm calls are replaced in
the instruction harness. The existing runtime test executable passes. Fatal
scoped clang-tidy passes the shot adapter, weapon definition importer and shot
contract tests. This verifies those numerical/ownership contracts, not the
complete visible two-way encounter.

Canonical player collision is also kept separate from costume appearance.
`45b490` passes one canonical actor namespace to model and exact EHIT lookup
(`4589e0`), then `5fe970` / `5fe070` register the supplied definition through
`5fe6d0`. The `pw` to `pw0` canonicalization in `6037d0` concerns body settings;
it is not proof of a missing-EHIT fallback. Collision admission must use the
canonical actor definition independently of the selected appearance, rather
than attempt a costume-specific lookup and silently substitute a different file.


### Animated affine hit geometry (October 27)

The purchased EA0 pose admitted at bone 28 has squared row lengths near
0.9982969 (scale approximately 0.999148), near-zero row dot products and
positive determinant. The animation transform retains authored quaternion
magnitude. Rejecting it as a rigid rotation was a boundary defect; relaxing
that check or normalizing the pose changes the original geometry.

Hit-query instances therefore retain immutable shapes and full copied affine
transforms separately from solver bodies. The original query receiver's bone
transform remains authoritative. Rigid death-body handoff is a distinct source
contract and is not inferred from hit geometry.

Pinned Jolt `TransformedShape::CastRay` maps the segment into shape-local
coordinates and delegates to `Shape::CastRay`, preserving the segment fraction.
Its transform representation is rotation/scale; this adapter instead uses the
existing shared affine inverse to retain shear as well. Jolt `AABox::Transformed`
provides conservative transformed bounds, and `RayAABox` rejects noncandidates
before the provider shape BVH. Surface normals use the affine inverse transpose.
Shapes are center-of-mass-relative in Jolt: the adapter subtracts the immutable
shape center for local rays and adds it before transforming local bounds.
Generation-checked query-instance identities cannot be used as rigid bodies.

The instance and command pools are preallocated from the engine domain. This
initial bounded implementation scans cached instance bounds, then traverses only
candidate shape BVHs; it does not allocate transformed mesh wrappers per frame.
The configured instance capacity is storage policy, not an original actor limit.


### Rigid physical handoff versus affine hit receivers

The original physical-follow path `4f6d10` obtains bone-world matrices, then
`4f3da0`/`4f3f10` publish queued move/reset commands. `4f3a90` only transposes
those values into `NxMat34`; it performs no Cavia-side scale extraction. Body
creation `4f4ee0` likewise copies the matrix into the SDK global pose while
primitive dimensions and local center remain fixed recipe values. Solved-pose
publication `4f6dd0` reads rigid SDK poses and reconstructs skeletal locals.
The legacy SDK's precise matrix-to-orientation numerical behavior remains
unresolved; the game executable alone does not establish it.

For the approved Jolt replacement, the physical adapter follows pinned Unreal
`TTransform::SetFromMatrix` in `Core/Public/Math/TransformNonVectorized.h` and
`TMatrix::ExtractScaling` in `Core/Public/Math/Matrix.inl`: separate row lengths,
assign any reflection to X, extract and normalize rotation, retain translation.
The shared decomposition returns scale explicitly. CharacterPhysics keeps those
scales for diagnostics and preserves fixed source recipe dimensions; it never
changes the original animation or EHIT transform. Singular or genuinely sheared
physical frames reject explicitly rather than silently choosing another solver
shape. This is a documented provider adaptation, not claimed PhysX 2.6.4
numerical equivalence. Unreal revision remains
`f45729c0b6f097038e72b52d366a62baeca3275b`.


Affine boundary verification: the actual-provider unit suite and runtime suite
pass shear, reflection, center-of-mass offsets, nearest eligible receiver,
atomic batch rejection, paused publication, generation reuse and teardown. The
counting CPU backing confirms no allocation during affine query/publication.
All six scoped provider/EHIT/world bridge clang-tidy translation units pass.
The full original mission-startup corpus passes all 58 authored group rows with
the physical decomposition enabled and reports zero live payload after teardown.
Both physical adapter/test clang-tidy translation units also pass.

### Native behavior prerequisites retained separately

Original action dispatch includes ordered `subd` companion records, omitted by
the earlier wrapper parser. The original primary/companion linked chain is
created by `504990`, entered/exited by `504b50`/`504b80`, and ticked by `503210`.
The purchased six-tree EA0 closure has 197 companions; automatic gun actions
are among them. Their outcomes do not replace the primary action outcome.
Across all 155 wrappers, 2,690 companions are present, at most two per primary,
with no nested companions. Companion evaluation strings may be empty.

The wrapper's `luac` payload begins with its big-endian bytecode length; outer
chunk alignment padding is not Lua bytecode. Preserving the declared length
fixes admission of the five nonroot EA0 wrappers. The native action library now
prepares typed parameters once, retains four bounded action slots, and executes
primary/companion wait, reaction, behavior change, random cycle, ready/fire,
voice, target and broadcast contracts. Movement and sequenced reaction remain
under implementation and are not claimed closed by parser tests.

The source `5a96f0` aim-displacement helper was executed from the purchased x86
image for 36 seed/direction/cone cases, including both vertical directions.
The production helper agrees with original float outputs (maximum discrepancy
below 5e-9 from decimal result serialization), and random state/draw counts are
exact. This validates that helper, not the full NPC firing/prediction pipeline.

The retained NPC navigation controller now separates `5ce940` distance
hysteresis from route storage and actor movement. Entry chooses idle, approach,
or retreat; it does not spend a second motion interval. Approach thresholds
receive the original 0.25 bias and caller-supplied minimum; the separate source
movement-context contribution remains an explicit input. Route/ground queries
return values; the actor alone applies resulting locomotion. The unit witness
checks transition latency, distinct start/stop thresholds, retreat destination,
pause and invalid-frame rejection. The actual-provider runtime test suite passes
with this controller present. Full live route integration remains outstanding.

`5bc590` follow movement uses the posture record's radius, not character collision
radius or speed. `5d5a00` selects one of thirteen records. `5bd150` keeps angular
movement offset relative to a moving target. `5ca280/5c9b40` retains a chosen
flock destination and source flags select direct versus routed movement and
actor versus target origin. `5bb670/5bb910` side movement periodically generates
four target-relative candidates and chooses among the nearest candidate and
its neighbors using target-forward projection. `5bc1b0/5bbe90/5bbfd0` patrol
retains authored path index/direction and preserves endpoint completion codes.
These are separate policies over shared navigation, not alternate solvers.

The hidden `5c8be0` cover-action switch was recovered in a bounded export from
its five table destinations. Hideaway retains a search request across states:
retreat/search admission, waiting, successful destination following, and hidden
visibility recheck. `5b25f0` creates that request; `5b1620` refreshes it;
request status and selected destination are read separately. Its retained search provider is now implemented for authored points, with
the dynamic-actor candidate and clearance boundary described below. It is not
replaced by a guessed nearest cover point. `5b75c0` hidecome also retains a shooting exposure interval and
wall-follow orientation before returning to its saved origin.

Impulse constants decoded directly from the purchased image are 30 at `82d208`
and 100 at `82a4e0`. `4f6920` therefore normalizes the incoming direction and
scales by thirty times the supplied magnitude before queueing mode one.
Fallback body selection compares eligible body positions with the segment from
application point to application point plus one hundred times that vector.
This describes recovered source units; the value is not an engine tuning knob.

### Pending force integration

The character adapter now retains the winning pre-handoff force by source
priority, with equal-priority squared-magnitude comparison. At handoff it queues
the dynamic-body transitions before the retained impulse. Original fallback
body selection uses the shared point/segment distance helper; it scans the
recipe bodies in their source order. This adapter transfers all recipe bodies
together, so it has no independently kinematic limb to exclude.

The ordinary completed death path requests the original upward direction
(`6c1dc0`), magnitude one, priority one and automatic body selection at the
actor root. The source's thirty-fold impulse scale is preserved. This does not
yet complete all incoming-hit force producers or heavy/blast response.

### Immutable navigation archive and retained routes

Mission admission now decodes indexed `arcs` groups, their `strs` names,
patrol IDs/points and search vertices/links into owning mission-domain values.
Offsets follow the directory rather than assumed adjacent chunks. Native
`5dc3c0` is an exact, first-match group-name lookup. `5da310` binds the primary
actor path string at environment `54` and the secondary at `78`, substituting
`notitle_0001.pat` only for an empty source string. `5d6590` selects the primary
group for a nonnegative signed selector, the secondary for a negative selector,
and uses its absolute value as the patrol ID. A missing group/path returns the
actor's initial position. Navigation `5d6510` prefers the primary group, then
the secondary; selecting group zero is not an equivalent fallback.

The concept mission's package has no navigation archive. The similarly named
archive in another mission is not borrowed. A read-only corpus pass admitted
23 of 24 extracted archives; an authored test archive declares three points but
contains two and is rejected. This is a malformed input boundary, not a reason
to remove decoder bounds. Synthetic tests cover owned name/geometry lifetime,
padded directory offsets, graph endpoints, route progress and absent paths.

Per-actor route progress uses the retained original graph with a bounded A*
open set and waypoint progression informed by the pinned Unreal graph/path
following reference. The original graph traversal itself remains opaque; this
is documented inference. Geometry determines side preference in equal-cost
choices; arbitrary vertex-index parity does not. Source approach/retreat
hysteresis remains separate from route search and physical actor movement.

Cover search now retains a request and completion across admitted frames and
scores authored cover points with recovered range, target projection,
visibility and travel predicates. Dynamic cover candidates come from native
actor registry `43c760`, actor radius `a38`, and position `5ecd30`; render bounds
or a set of static collision triangles are not substitutes for that registry.
That dynamic candidate provider and its body-placement clearance contract are
not yet closed. Authored-point queries can operate independently. Hidecome's
fifteen-sample reversal compares the difference of retained navigation vectors;
it does not repeatedly flip from an absolute heading. Its original wall-follow
worker predicate (`5cf680`/`5af5a0`) is narrower than generic movement activity
and remains a provider integration boundary.

### Post-solve receiver publication

The world now exposes a world-only `publish_queries(WorldFrame)` phase after
one successful solve. It consumes only queued affine query transforms, retaining
body transforms/impulses for the next actual solve. The matching admitted frame
is required; foreign, stale, unsolved and repeated publication fail explicitly.
Paused frames publish nothing. Physical corpse poses can therefore update their
query receivers before same-frame projectiles without a second physics step.
Actual Jolt contract tests check immediate receiver visibility, deferred body
commands, phase rejection and no publication allocation; scoped fatal lint
passed for the provider and test translation units.

### Player defeat and original GameOver presentation

The normal callback registry scans player health for cause 1000 (`481a10`),
then consumes the first ready record after descending-cause stable sorting
(`481ef0`). The original default callback owns its two-second waits, audio stop,
magic/entity cleanup and ExecGameOver/WaitGameOver calls. Native death replaces
the Damager update handler: `6737a0` installs `66e790`/`683d40` instead of normal
recovery `66e9c0`. Recovery suppression belongs to that death lifecycle.

`CancelRealTimeEventAll` is **not** cancellation of this callback registry.
Its native implementation `478150` calls `46f570` on the real-time event owner,
which retires ten stride-64 records and their associated objects. Clearing the
64 callback records in response to that API would be incorrect.

The new mission UI owner reconstructs `TaskGameover` (`4a4380`, `4a3bb0`):
original sprite IDs 370/371, localized system keys 0018/0001/0002, initial Yes,
nonwrapping horizontal choice, confirm priority and source cues 1/3. Entrance
channels advance at .45, 2.7 and 3.1 units/second in order. The selected button
uses the shared source sprite light-vector/COLOR saturation; confirmation uses
the original .3-second pulse before the .5-second black fade. The UI publishes
restart or return only after completion. It owns no mission or world clock
mutation; the caller performs the transition. Source draw constants put the
64-pixel prompt at (640,283), 32-pixel choices at (448,380)/(832,380).
Resource preparation owns its archive leases and GPU handles; frame publication
uses bounded commands. Native visual acceptance remains a separate integration
check, not established by the state tests.

### Defeat population retirement

`RemoveEnemyAll` (`47a9d0`) selects live actor classification bit two, requests
retirement, then clears matching deferred records through `445f10` with kill
counting disabled. Resistance (`47aad0`/`445f60`) selects bit four; citizen
(`47a910`/`445e40`) selects bit eight. The pending implementation scans 256
records of stride `170`; the citizen/resistance loops are unrolled four at a
time. None resets placement trigger history or another family's records.
The deferred family comes from case-insensitive `44af80`: PW one, HM/HW eight,
HR four and all other families two. Authored row-six flags are a different field.

`MissionPopulation::remove_family` now applies those typed requests to matching
live and pending records. Each live receiver retires before its identity is
compacted; a failure leaves the failing identity discoverable. The world adapter
uses its existing ordered combat-controller, presentation and actor teardown.
Tests cover each family, case-insensitive classification, unrelated survivors,
matching pending cancellation, repeat removal and preserved fired triggers.
These are deferred-population semantics. The separate source citizen/resistance
controller singleton reset and any direct-spawn disposition overrides are not
silently claimed by this API.

GameOver admission was also exercised against the purchased stable catalog:
1280x720 background, 300x80 button and 1024x1024 font; the interactive frame
emits nineteen ordered draws with four resident textures and releases all on
teardown. This used a controlled output adapter and establishes resource/layout
admission, not native GPU visual acceptance. Focused original input/phase tests
and the combined runtime contract suite pass. The resource suite reports 617
checks; source byte counts are not additional tests.

### Integrated action admission and retirement scope

The actual six-wrapper encounter exposed a preparation error: structural `AI`
selectors and negative label-transfer records were being sent to the native
action factory. The behavior runtime already owns those records. The action
library now preserves their node indexes without constructing an executable
chain and rejects direct dispatch of one. Concrete primary/companion actions
retain their original order. The synthetic regression exercises a structural
root plus a concrete child; actual encounter validation remains a separate run.

Actor observations follow the mission manager's admission-order identities,
including retirement and later readmission, rather than reusable object-storage
slots. This preserves the encounter traversal order used for perception ties.

`_RemoveBgObjAll` (`47a8b0`) calls background manager `433e20` / `446050`.
That manager walks active linked records, retires each retained actor, clears
the actor reference, and marks the record inactive. The producer is the same
`445370` called by `_SetBgObj` through `449460`: it includes authored background
placements, not only behavior-bearing dynamic actors. `449460` excludes the
`musi` family during registration. Thus removal must retire both resident and
pending prop records, including the props represented by the initial frozen
snapshot, while retaining main stage geometry. The existing immutable baked
prop contacts require a separate prop lifetime boundary; clearing all scenery
or leaving all props active would both diverge from the source.

The prop contact lifetime boundary is now implemented with bounded registration
`CollisionGroup` values. Original stage primitives retain group zero; placed
background primitives use the game-owned group one. The exclusive world owner
can disable a group between queries without rebuilding or freeing geometry.
Camera, segment, support and character-contact queries all honor that gate.
Tests retain terrain while disabling/re-enabling prop triangles and capsules.
This does not invent a projectile EHIT bank or physical material for generated
prop PHIT geometry; that independent provenance remains required for admission.

All six actual encounter wrappers now prepare successfully: 1,893 authored
nodes, including structural selectors and ordered companion actions. The actual
32-byte `flocktomessageB` record has eight words rather than the nine-word actor
flock record: timeout, posture, speed, distance, distance range, angular range,
angular center and flags. Its target comes from the retained environment message
position, and its projected goal height is actor height plus .5. Admission is
not a substitute for live receiver validation.

### Named paths, incoming hits and directional feedback

The placement producer `47ef40` copies character-table column five into the
primary environment path at `+54`, and group-table column seven into the
independent secondary path at `+78`. Missing strings use `NULL-PATH`; an actual
empty string stays empty. At navigation admission `5da310` translates an empty
name to `notitle_0001.pat`, looks each name up through exact matching `5dc3c0`,
and retains an absent result when no archive/group exists. Primary and secondary
are not an override pair. `5d6590` chooses primary for nonnegative selectors,
secondary for negative selectors, then uses the absolute selector as path ID.
There is no justified default archive group zero.

Ordinary shot production `61b1d0` / `61bbf0` / `5c7d30` copies weapon level
specification lanes `+0c` / `+18` into message `+30` / `+34`. The living ordinary
Damager receiver `6867b0` does not consume either lane. They therefore do not
justify introducing an impulse into the living movement controller. The
physical-death receipt `66e7b0` instead supplies a constant magnitude two,
priority one and nearest-body selection to the already recovered `4f6920`
policy, using message direction and contact point. `receive_corpse_shot` applies
that request only after physical handoff. Other receiver families' uses of the
two specification lanes are not claimed by this bounded actor contract.

Ordinary direction notification occurs after `6867b0`'s non-self hit handling,
including invulnerable and lethal paths; it is not conditional on reducing
health. The physical-death receiver does not issue this HUD notification.
`6874d0` negates incoming travel, transforms through the inverse horizontal
camera basis, normalizes the horizontal result and tests front/rear before the
two sides, with the source cosine of approximately pi/4. Its zero-horizontal
case reaches the left branch. This pure policy lives under combat, separate
from the retained HUD owner.

The HUD's four channels preserve `4cfb30`/`4cfb70` task phases, rejecting a
visible channel's retrigger and retiring after the original one-second progress
interval plus task transitions. `4cee70` emits one short edge flash (sprite 23)
and two moving bars (sprite 22) per visible channel. The bars use original
quintic travel, cosine/sine opacity and independent nonuniform scale centers;
the flash uses its original .025-second hold and .183-second fade. The source
sprite transform applies scaling about its selected center, rotation about a
separate center, then translation. The HUD surface resolves these copied values
after original image dimensions are resident. It does not create new textures
per hit or retain an actor or world-clock pointer.

The ignored `damage_direction_witness.py` executes the purchased x86 draw at
five progress values for all four directions. Its 60 sprite descriptors match
the reconstructed owner in art identity, coordinates, scale, rotation, packed
opacity, scaling center and translation; largest coordinate difference is
below .000062 pixels. This is a numerical presentation witness, not a native
screen acceptance claim. Focused unit checks cover direction sectors, camera
orientation, independent channel admission, retrigger rejection and retirement.

### Explicit absent mission producers

This encounter has not admitted real-time event actors or environmental audio
voices. `MissionAncillaryState` exposes that immutable capability boundary.
Cancellation over its empty event actor set is valid and never clears the
independent mission callback registry. Environmental sound zero is a valid stop
of the inactive producer; every nonzero activation fails explicitly. Compile-time
invariants prevent enabling either capability while retaining the empty-owner
implementation. No fabricated event registry, voice handles or lifecycle counts
are added. Actual producer admission must replace this boundary with the owning
subsystem rather than treating these two stop operations as completed systems.

### Provider ownership review and integrated checks

The author review confirmed that Jolt declarations remain inside the private
backend and its private build dependency. Runtime registration admits one live
allocator owner; world storage retains that owner, while query/mutation/resource
capabilities hold checked weak borrows. Shapes, bodies and affine query instances
use generation-checked identities. Query exclusions apply before closest-hit
selection, and retirement removes pending commands for the retired identity.
World stepping remains the only solver-advance authority. The separate post-solve
publication consumes only queued query transforms once for the current solved
frame; it does not replay impulses or advance the solver a second time.

The isolated provider memory tests and integrated runtime contract tests passed
with the HUD and corpse receipt changes. The corpse regression verifies that a
receipt leaves velocity unchanged until the next admitted world step, then
applies the recovered impulse through the real provider. This author review is
not an independent-agent review or a claim of native encounter acceptance.
Numerical equivalence between the original PhysX solver and Jolt remains an
explicit adaptation boundary. Generated placed-prop projectile receivers still
require their own source EHIT provenance; character/camera collision geometry
does not silently substitute for that bank.

### NPC motion visibility

The actual EA0 factory `4563e0` constructs the generic `5fe070` actor. Its
registered visibility listeners are target one (no-op) and target 1000 (selected
weapon). It adds no registrations for 998 or 999. Generic `5f8470` first tries a
registered listener, then acquires or releases an entry through `5f7800` /
`5f7ac0`. The latter update the model's eight-word group mask through `42a7b0`
and `42bd40`. Those accessors explicitly return zero or skip writes for word
indices eight and above. Thus authored 998/999 fallback entries have no group
side effects; they are not missing visible objects or unchecked draw indices.
Target zero also has no acquired entry to release. Target 57 is a real group
bit and must change retained presentation.

`ModelVisibility` retains the observable eight-word mask and each motion bank's
acquired in-range bits. Effectless out-of-range entries need no allocation: their
acquisition, release and reset cannot change the model through the original
bounded accessors. Reset clears acquired bits rather than restoring the initial
mask, and separate banks are not reference-counted. The NPC actor resolves its
registered listeners before this fallback; weapon activation/reset enables
group one and release disables groups one and two. The optional release effect
in `5ff660` is restricted to WA0/WA2/WA3, so it does not apply to selected WG1.

All NPC draw palettes and presentation entries now remain resident, including
initially hidden groups. Animation changes copied visibility without recreating
geometry or overwriting the lighting owner's shadow membership. Material and
palette admission failures remain explicit for previously hidden draws.

### Authored attack sphere queries

The ordinary actor attack receiver `5f7650` queries a discrete sphere at the
current joint position, using the copied radius and source bank 8. The previous
joint position belongs to a separate scenery-impact producer; it does not turn
this actor query into a swept segment. The original all-hit path
`54a800 -> 54b380 -> 555fe0` retains individual overlapping faces, then
`54bc30` keeps up to 32 results in ascending signed separation order. Actor
attack-serial deduplication happens after collection. The contact is the
triangle closest point, and separation is its distance from the sphere center
minus the radius (`550810` / `551010`). These are recoverable in passes 361,
364 and 365; the generic query implementation is also present in passes 43
and 68.

`physics::Queries::overlap_sphere` exposes this geometric boundary with typed
receiver and shape identity, copied user metadata, closest point and signed
separation. The caller owns the output span and selects the 32-result gameplay
budget. Overflow is reported, with the deepest contacts retained. Layer,
receiver, instance and copied flag exclusions run before geometry consumes
that budget. The engine does not deduplicate actors or decide damage. Queries
borrow the existing world and do not own simulation advancement or resources.

The implementation uses the pinned Jolt `CollisionDispatch` for solver bodies
and `CollideSphereVsTriangles` for affine query meshes. Mesh BVH traversal
collects fixed batches via `Shape::GetTrianglesStart/GetTrianglesNext`; each
triangle is transformed by the retained complete affine matrix before Jolt
computes the world-space closest point. Query output and traversal require no
heap allocation. This handles nonuniform scale and shear without changing
skeletal transforms or creating per-frame shape wrappers. Overlapping affine
non-mesh query instances reject explicitly; the selected actor hit geometry
uses meshes.

This is an explicit provider adaptation: the original transformed-triangle
path finds its closest point in local coordinates and applies a directional
scale correction. Exact world-space closest points under nonuniform scale or
shear may therefore differ. Equal-score contacts preserve provider traversal
order, which need not match original face traversal. Neither distinction is
claimed to be bit-identical PhysX behavior. The source contact budget and
subsequent actor deduplication remain separate policies.

Actual-provider tests cover a sheared/nonuniform instance, both triangle faces
of one receiver, world contact/separation, identity/flag/layer exclusion, stale
identities, a rigid sphere contact, 40 overlapping faces retained in a sorted
32-entry span, explicit empty-span overflow, and no query-time allocations.
The isolated `memory-tests` executable passes with these contracts. Fatal
scoped clang-tidy passes for the provider and contract tests, including the
public header through its implementation. This does not by itself establish
native encounter or audiovisual acceptance.

## Melee receiver and lethal launch

The actor contact packet uses a different native receiver from an ordinary
projectile. `685170` rejects the same actor identity, performs the source line
query, requests category/index audio and effect 800, and computes radial damage
through `5fd090`. Body-region projectile multipliers do not participate. The
world owns identity, line-of-sight, contact serial deduplication and presentation
requests; `MeleeImpact` carries copied source position, contact and the two
radial damage endpoints to the actor health owner.

Prepared body policy now includes the heavy-melee threshold at `body+6c` and
two close-death weights at `body+148/+14c`. The surviving reaction distinguishes
ordinary, heavy and crouched responses and preserves the native jump/reaction
state admission. Invulnerable health admission does not itself suppress the
reaction. Lethal PW0 selects the separate four close-death slots, including
while airborne. The original random draw is consumed before the family test.
EA/HM/HW/EE can instead select `StateBlastStart`; that branch is not an immediate
ragdoll or a substitution of the ordinary projectile death table.

The retained `MeleeBlast` policy reconstructs the directional launch, flight
and landing sequence from `683100`, `683470`, `682b20` and `682e60`. Source
animation completion controls the transitions. The launch turns at the native
angular rate and snaps its final direction; flight initially follows authored
root motion, then integrates the native 9.8 gravity after clip completion.
Existing shared character contact provides floor/wall resolution. Landing
clears the query exclusion flags, and crossing original frame 5 transfers the
completed skeletal pose to the already admitted physical bodies with the
source randomized upward impulse. Its random samples come from the session
stream; the state owns no clock, collision solver or independent generator.
The native direction helper receives the retained heading as a point and
subtracts actor translation; this unusual caller behavior is preserved rather
than silently replaced with a conventional heading-only selector.

`ActorHitInstance` now retains copied query flags. Updates are bounded batches
through the engine mutation capability, validated before enqueueing. Only the
world's normal simulation or post-solve query publication applies them. Paused
worlds retain pending updates; retirement removes pending commands for the
retired generation. Launch/flight and Dodge use the original mask `0xA`, with
explicit ownership transfer so leaving Dodge cannot clear a newly entered
blast state's flags. The backend has no animation or character names.

Already physical corpses use another melee branch: native `685170` supplies
magnitude 4, priority 1 and explicit body index 1 after its vertical direction
bias. This is distinct from the ordinary corpse-shot force and from landing's
randomized force. Both paths enter the same source-priority arbitration and
world-owned provider mutation queue.

Evidence is retained under ignored `first-combat-physics` and engine passes
365, 368–370, with older family transitions/lifetimes and direction-reference
exports reused. Unit contracts exercise radial endpoints, reaction admission,
separate weighted deaths, all launch directions, animated flight, gravity,
landing and the frame-owned handoff. Provider contracts exercise flags before
and after publication, pause, immutable observations and post-solve publication.
Full original-content blast presentation is an integration check, not implied
by these policy tests. Existing character-contact/provider adaptation limits
continue to apply; this work does not add world death-volume producers or
unrelated nonlethal spell blast owners.

Scoped verification for this addition: isolated `runtime-tests` and
`memory-tests` build and execute successfully. Fourteen affected translation
units pass the fatal clang-tidy policy, including the provider, actor adapters,
player/NPC receivers and melee policies. Query mutation and publication retain
separate no-allocation assertions; provider solver warm-up is not counted as a
query allocation. The final provider test changes also pass scoped tidy and
format validation. This is not a claim that the repository-wide gate or the
original-content lethal launch presentation has completed; integration owns
those checks and the separate victim voice action delivery.

### Review closure: NPC damage observations

The NPC category producer (`600e70`, NPC vtable `852334+30`) is distinct
from the player producer (`600f80`). It reads completed weighted eye attachment 54 height above
the actor position and compares it against one half / one quarter of model
group zero's header `+40` radius. DamageReaction slots 757–760 override the
height result with category one. Explicit StateSquat also returns one; that
state is not currently admitted by the NPC owner, and animation posture bits
are not substituted for it. Generic flight alone does not imply category two.
The copied policy now feeds ordinary hit selection and melee crouch selection.

Ordinary category-two death enters physical ownership immediately, preserving
the StateDie constructor's zero impulse radius and the random draw consumed
by its direction helper before the upward impulse. Ordinary animated death
also hands off on current completed contact loss, independently of clip
completion, after publishing the completed pose to its physical bodies.

The surviving melee gates (`685170`) are BlastStart/Blast predicates, with
the ordinary Shooter exception. They do not suppress generic jumping. The
earlier generic airborne gate has been removed from the shared policy.

The actual-content melee witness constructs EA0 from the original mission
placement, support catalog, model, appearance, collision and physical recipe,
then drives the public melee receiver with source RNG seed one. It found an
admission defect that synthetic phase tests could not find: the NPC pose
semantics omitted the traced ordinary StateDie melee slots 745–748 and
BlastStart/flight/landing slots 839–850. Those receiver ranges are now admitted
explicitly, without classifying unrelated slots or actors.

The repeated witness passes: lethal melee requests the original start branch,
flight slot 840 is active on the first advanced frame, landing slot 839 arrives
at frame 41, and the articulated physical handoff occurs at frame 46 with three
source RNG draws. It uses public production ownership, Metal resource admission
without a visible game window, and the actual Jolt world. This is a mechanical
content witness, not user visual acceptance. Runtime contracts and fatal scoped
tidy passed for the changed reaction, observation and pose-admission units.
The ignored diagnostic source, build arguments and stdout are under
`research/evidence/reconstruction/first-combat-physics/melee-actual.*`.

### Reopened EHIT palette correction

The original `5fa290` calls `4225b0` with skin output enabled: inverse bind
multiplies completed bone-world matrices before rigid hit-part publication.
EHIT vertices are authored model-bind coordinates. The earlier raw bone-world
interpretation displaced head/torso hit surfaces away from the rendered actor.
The shared receiver now borrows a completed `SkeletonPose` and selects its skin
palette internally, for both Alicia and NPC initialization, animation and
physical handoff. The original executable witness over actual skeleton records
agrees within 6.51e-7; real before/after query and native hit verification remain
separate requirements. Group visibility remains subject to the existing
selected-appearance boundary; this does not claim all future actor mutations.

### Coupled dead-body contact and model radius

The extended native death continuation exposed a missing source consumer. Common
publication `602b70` doubles the negative model offset for a dead actor whose
authored radius is at most one. The same predicate also doubles the radius in
custom contact `6030d0`; `604930` subtracts twice that effective radius from the
standing/alternate total height. Updating only publication lowered the completed
corpse animation before handoff while support kept the living center height.
The physical skeleton was consequently admitted below the valid floor.

The ignored failed capture records upward-facing, material-zero physical terrain
at the corpse location and fifteen actual provider body origins below it. This
is a producer mismatch, not missing terrain, reversed winding or a justification
for moving Jolt bodies after handoff. `character_body_radius.hpp` now supplies
the same copied value to NPC and Alicia custom contact/model publication. The
player receives explicit life state from the authoritative mission damage
receiver; no animation-kind guess controls geometry. Initial NPC placement uses
the effective radius while the actor retains the immutable authored radius,
preventing a second doubling for initially dead content. Skeletal recipe shapes
and provider collision behavior are unchanged.

The shared ground-contact regression changes a grounded living actor to the
source dead radius and checks that support raises its center while preserving
the model above the same plane. The final full repository gate and original-content replay pass. The exact
failed native continuation now settles the body above the floor and retains
that support through more than ten seconds after handoff; ordinary movement
around the container exposes the settled corpse visibly. Actual body poses
and floor queries accompany the captured frames. User acceptance remains open.
