# Alicia animation and renderer bindings

October 23, 2026. This note records the original actor assembly and the current
slice 5 reconstruction boundaries. Actor animation does not provide movement,
gravity or collision response. Refer to [actor lifecycle](actor-lifecycle.md)
for hierarchy ordering and [animation data](animation-data.md) for sampling.

## Resource selection and ownership

The costume callback `0x4c2cf0` mounts `concept:\\PW%d\\pw%d.rat` at priority
40 and selects `pw%d`; the base model is `pw0`. The support alias in `0x5e2110`
uses `pw0` for every `pw*` costume. The player constructor `0x5fe970` retains
`pw0`/`wa0` actor/weapon labels. The common actor constructor `0x5fe070` resolves
`RightWeapon` and `RightWeapon_b`, creating child attachment objects. Their
update `0x56b610` consumes the evaluated model bone through `0x56ab10`, after
parent actor and model updates.

Use the existing `content::load_model_definition` for the selected costume and
weapon, and `content::load_character_support` for the shared motion owner.
`CharacterSupport::motion_order` preserves the native publication list; it is
not the game's motion state enum. Archives must outlive the borrowed channel
and bone-name views. Current mission placement preload does not load Alicia.

A read-only corpus census used those production decoders, with stable priority
30 and the five installed costume catalogs at priority 40. It retains no assets
in tracked source. Diagnostic program and output are under ignored
`research/evidence/reconstruction/actor/`.

| Model | Bones | Draws | Shape position buffers |
| --- | ---: | ---: | ---: |
| PW0 | 125 | 46 | 4 |
| PW1 | 125 | 48 | 4 |
| PW2 | 125 | 46 | 3 |
| PW3 | 125 | 56 | 0 |
| PW4 | 125 | 37 | 3 |
| PW5 | 125 | 34 | 0 |
| WA0 | 4 | 7 | 0 |

PW0 support constructs 161 motions, 164 parameter publications and six shape
animations. The support skeleton publication is 13,008 bytes; its legacy `bone_names`
field identifies skeleton data, not the model name table. The six costume
`motion.bil` name tables are byte-identical: 125 MBDB entries mapping names to
bone indices. `RightWeapon` is index 26, `RightWeapon_b` is 84 and `Spine2` is 6.
These counts describe loaded content, not completed behavior or visible groups.

## Motion binding and sampling

Native `0x601250` builds 1,108 game motion slots from the pointer table at
`0x975b28`, with player overrides at `0x853408`. It resolves compressed before
uncompressed publication. A leading `@` is stripped from the publication name
and writes zero to motion header +16; ordinary labels write one. Thus the
native state mapping controls repetition as well as selecting a named clip.
The PW overrides seen here chiefly replace weapon reload/shoot labels.

| Native slot | Base label |
| --- | --- |
| 0 | `walk_l` |
| 1 | `stand_l` |
| 656 / 657 | `@dash_jump_m` / `@jump_m` |
| 672 / 673 | `@dash_jump_s` / `@jump_s` |
| 688 / 689 | `@dash_jump_e` / `@jump_e` |

These are binding labels; suffix meanings and the exact current-state selection
must follow state/component callers. A pose preview must not turn them into
invented gameplay transitions. Jump includes world movement, ground departure
and landing decisions independently of its poses.

The actual `pw0_stand_l._ba` header has duration 180 at +8, rate 60 at +12,
repeat flag 1 at +16 and 125 bones at +20. `pw0_shoot_stand_l._ba` has duration
1 with the same rate/repeat. Controller `0x41f140` advances by rate × elapsed;
`0x41f0a0` wraps at or beyond duration when repetition is enabled and clamps
otherwise. Do not assign 30 fps from the material-animation corpus.

`MotionDefinition::tracks()` already applies the file's per-bone source-index
directory. `0x423d20` can additionally use an optional controller remap from
`0x41eda0`; costume compatibility must validate bone identities rather than
assuming every equal-sized skeleton is identical. A separate-channel record
contains fallback scale at +4, quaternion at +16 and translation at +32;
the separate channel pointers follow scale/rotation/translation order. An
initial interpretation reversed scale and translation. Sampling actual
`stand_l` records exposed that error: +4 is `(1,1,1)` and +32 is the bone offset.
The implementation and corpus checks use the corrected order.
Whole-transform `0x429780` samples ten floats ordered quaternion (4), scale (3),
translation (3). Separate translation/rotation/scale channels remain distinct.
Absent tracks and multiple weighted/additive controllers need explicit policy.


## Initial body and weapon assembly

Player construction `0x5fe970` sets body group word zero to `0xfffffffd`,
disabling group 1. The common constructor starts local Y at -0.5, but live actor update
`0x602b70` overwrites it with negative effective body radius (-0.4 for living
Alicia). Keeping the constructor value caused the observed 0.1-unit foot offset. Model update
`0x56b850` additionally constructs a cached Y rotation of π, multiplies that
matrix by the actor transform at +0x50, then publishes it to the embedded model.
The instructions at `0x56b98f` pass the actor matrix to `0x406690` with the cached
rotation copy as receiver; that helper computes receiver × argument in the
row-vector convention. Thus the body world transform is `rotationY(π) ×
actorWorld`, retaining the model's local vertical offset. This orientation is
source behavior, not a camera-facing correction. The NPC
helper `0x4559f0` has other group edits, but its `0x455b50` resource switches
handle HR/HM/HW actors; those edits do not establish a player visibility rule.

Player factory `0x45b490` acquires four separate weapon resources `wa0` through
`wa3`, registers them in slots 0–3 and selects slot 0 through `0x603900(0,0)`.
WA0's seven draws have group sequence 0,0,0,0,1,2,0; they are not four gun
variants bundled into one resource. No extra group mask appears in the traced
activation path.

Weapon activation `0x615900` constructs the model, applies local Y rotation
π (the original constant is 3.1415927410125732), parents it to the weapon owner,
and selects model motion 0 followed by motion 1 or 2 with a 0.3-second blend.
`0x603900` chooses 1 when actor+0x126c is at most zero and 2 otherwise. The factory
`0x456a90` obtains its seven-entry motion table from `0x617210`, which substitutes
the three-character resource prefix into the templates referenced at `0x976da0`.
The slots are defaultmotion, stand_l, shoot_l, stand_reload, shoot_reload,
shoot_end, shoot_after. Thus neutral initial selection is `wa0_stand_l`, not
publication index 1 (`wa0_shoot_end`). The last slot has no WA0 publication in
this corpus. Numeric binding and archive publication order are distinct.
Bone query `0x56ab10` returns the evaluated bone/parent chain multiplied by model
world; this is a world attachment transform, not an inverse-bind skin matrix.

The weapon's activation-local half-turn is separate from the model renderer
conversion. Constructor `0x5dfc60` installs vtable `0x850d68`; its update slot
`+8` dispatches `0x5e37b0`, which directly calls `0x56b850` at `0x5e37b9`.
That common update applies another Y half-turn before publishing the embedded
model world matrix. The complete weapon render basis is therefore
`renderHalfTurn × (activationHalfTurn × RightWeaponWorld)`. Omitting the
render conversion reverses the gun relative to the hands. The two exact
half-turn matrices cancel, but the implementation preserves their distinct
source responsibilities. This correction applies to every weapon class.


The production support decoder finds six WA0 motions, all at 60 frames/second:

| Publication suffix | Duration in source frames | Authored repeat |
| --- | ---: | ---: |
| defaultmotion | 1 | 1 |
| wa0_shoot_end | 125 | 0 |
| wa0_shoot_l | 1 | 1 |
| wa0_shoot_reload | 130 | 0 |
| wa0_stand_l | 1 | 1 |
| wa0_stand_reload | 205 | 0 |

The weapon census and bounded activation exports are retained under ignored
`research/evidence/reconstruction/actor/` and
`research/evidence/engine/pass-70-player-weapon-bindings/`.

## Native rendering contract

The actual PW0 meshes use up to four influences. The common full declaration
contains FLOAT3 position, SHORT4N normal/tangent, HALF2 UV, UBYTE4 indices and
UBYTE4N weights. Both index and weight attributes use source BLENDWEIGHT usage,
with usage indices zero and one. Palettes contain at most 32 matrices per draw.
Source vertex shader 453 accumulates Y, then X, then Z, then W for positions,
normals and tangents; weights are not renormalized. Its selected skin family
includes effective selector `00820690100`. Other traced vertex families retain
one or two influences. Index validation includes zero-weight lanes the original
program still reads.

Some draws separate positions into a shape stream while normal/tangent/UV and
skin attributes remain in the ordinary stream. Rendering the original base
shape positions is an explicit rest-shape mode, not morph animation support.
Morph weights and transformed shape deltas remain a separate consumer.

The renderer retains immutable mesh/shape buffers and draw-local palettes.
`WorldPoseUpdate` updates a prepared model/draw destination range after validating
the complete batch. Palettes keep their published size; updates allocate no
geometry, textures, samplers or CPU palette storage. Duplicate instances with the
same model/draw receive the same palette, matching current texture-update
identity semantics. Independently posed instances will require an instance
identity before they are introduced.

Before designing this extension, inspected Unreal commit
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/GPUSkinVertexFactory.cpp`,
`UpdateBoneData`: section bone maps select reference-to-local transforms into
retained device bone buffers. Adopt the explicit palette/mesh ownership split;
retain Bullet Witch's four-row palette and original influence equations. The
current host retires each command before owner reuse, so no new asynchronous
buffer-ring policy is introduced.

Both design and implementation retrieve Core I.1/E.6 and performance GPU.1/MEM.9:
explicit borrowed update data, transactional resource ownership, retained device
geometry, and setup-sized update storage. CPU animation optimization remains
independent of GPU vertex skinning; this is not an argument to offload clip
sampling to the GPU.

The implemented actor families retain one/two/four influences and their normal,
environment, reflection, Fresnel and spherical-harmonic terms. The authored
`AMOC` texture's red channel scales ambient plus spherical-harmonic lighting;
it does not scale directional lighting or self-illumination (pixel programs
74, 123 and 412). Both color and shadow geometry use the same pose/shape inputs.
The current scene supplies an empty local-light list. This does not complete
nonempty local-light rendering.

PW2's additional two-base family uses vertex 478 and pixel 423, with pixel 424
for the spherical-harmonic variant (`82c082b0200`). An initial lookup missed
that explicit registry entry; the bounded program dump resolves it without
changing scene feature policy. These programs interpolate between base colors:
`base0.rgb + base1.a × indirectB.a × (base1.rgb × indirectB.rgb - base0.rgb)`.
They consume vertex color, and `FRSA` scales alpha by the original saturated
Fresnel equation; this is independent of reflection's `FRSR`. The engine
classifies these requirements explicitly, preserving stage-layer multiplication
for the other original family. Both the base and SH programs are implemented.

The native integration fixture exercises a four-joint material with AO and a
separate rest-shape position stream. Updating only the fourth joint changes
the rendered pixel; reverting restores it. The retained CPU domain byte count
stays fixed. A batch containing a valid update then an unknown identity leaves
the prior pose intact, and absent explicit rest-shape opt-in rejects the draw.
The four-joint fixture passed in `memory-tests`; targeted fatal clang-tidy
passed for the material classifier, native renderer and native fixture. The
additional PW2 fixture renders an exact expected BGRA pixel of
(96,64,32,64), distinguishing secondary-color interpolation from multiplication
and verifying independent Fresnel alpha with SH illumination. It passed in the
native integration executable. Full repository gate and game-scene integration
belong to the parent slice.


The offscreen production-owner probe rendered all six selected costumes for
90 real-time frames each in the original One Shot Kill scene. It compared every
selected model's 125 bone-name/index entries and parent links against PW0 before
constructing the actor. All six passed and produced retained geometry plus
animation without renderer validation failures. Visible actor/weapon draw counts
were 52, 54, 52, 62, 43 and 40 for PW0–PW5. PNGs, HDR readbacks, commands and
logs remain ignored under `research/evidence/reconstruction/actor/`. All six
captures were visually inspected for distinct costume geometry, forward body
orientation and attached weapon. This is initial-pose/idle compatibility, not
acceptance of every gameplay animation, morph, physics or cloth behavior.

### Locomotion root consumption

The ordinary Runner update consumes the model's accumulated root displacement
before custom contact correction (`0x664be0`). Real PW0 walking and running clips
contain translating root bone zero: one walk cycle advances about 1.40876 units
in one second; one dash cycle about 3.00651 units in 0.6 seconds. These are asset
measurements, not replacement speed constants. The renderer must remove that
translation when the movement owner consumes it, or motion is applied twice.

For cyclic extraction, inspected Unreal `UAnimSequence::ExtractRootMotion` and
`ExtractRootMotionFromRange` in `Engine/Source/Runtime/Engine/Private/Animation/AnimSequence.cpp`
at pinned revision `f45729c0b6f097038e72b52d366a62baeca3275b`.
Unreal accumulates nonwrapping ranges across clip endpoints. We adopt endpoint
inclusive sampling and accumulated full-cycle translation, with constant work
for multiple crossed cycles. The player now retains the full selected-controller relative root matrix,
including rotation used by the raised turning clips. Source constructor42a8f0
enables consumed-root handling;423d20 replaces the full local root with identity
before hierarchy so rotation and translation are not applied twice. Pelvis,
reference and attachment channels remain independently authored. The parent
consumes the preceding child-model interval through the current selected
reference, as detailed in [actor/model clock order](actor-model-clock-order.md).
Visual outgoing blends do not replace the current controller's movement
interval; selection and paused appearance publication do not advance it. Shared animation extraction has no collision/game policy.

The original physical evade input is handled by Dodger, not generic RunnerJump.
A maps to attack; default LT above 0.1 maps to the held evade action. The earlier
bounded host incorrectly exposed a generic scripted jump through A. Its 22-frame
start and 4.2 impulse were real source values for the wrong consumer. They are
historical evidence, not the corrected player contract.

The current movement owner selects all four authored evasive families. Start
uses its full 15 frames, middle moves all XYZ through the endpoint-local
`Waist_direction` reference basis, and contact can end the flight. Only after
middle completion does gravity continue from its last root-derived Y velocity.
End plays at 2x and releases at cancellation frame 25. The live model offset is
negative effective radius. See [player evasive leap](player-evasive-leap.md) for
the full dispatch, endpoint basis, steering and timing evidence.

The hardware vector scales by 10/9, clamps radially at one and is zero below
0.15. Nonfiring ordinary movement chooses dash above 0.9 with rate=magnitude,
and walk at or below 0.9 with rate=magnitude/0.9. The old 0.05 idle/0.5 run
thresholds were reconstruction policy and are replaced. Combat weapon-dependent
rate scaling remains outside the current consumer.

For time partitioning, inspected Unreal `GetSimulationTimeStep` at pinned
revision `f45729c0b6f097038e72b52d366a62baeca3275b`; bounded steps are no larger
than 1/60 second. Pause withholds movement, pose, camera and material advancement
together. Original mission start remains pending; mission-owned bounded placement
supplies the documented fresh/checkpoint values independently of player movement.

Ordinary Alicia has a live behind-player camera, which activates the source
one-unit extension of the downward grounded probe. The game passes this policy
explicitly; airborne queries use zero extension so a jump is not captured by a
floor one unit early. Static contact owns the geometric resolver, while this
actor-specific switch remains in the player controller.

Generic Runner landing evidence (frame 13 ordinary, frame 42 hard landing)
remains recorded in [jump timing](player-jump-timing.md). It must not be used as
the original hardware evasive-leap contract; that action uses the separate
25-frame double-rate recovery described above.

### Initial admission for all four weapon classes

`MissionLoading::prepare_player(costume, weapon_class=0)` now admits one explicit
class0..3 before constructing the actor. The mapping is the original factory's
wa0..wa3 slot registration (45b490/6047a0), selector603900 and617210's
resource-prefix motion resolution. Default0 preserves the initial game choice.
The actor prepares its weapon catalog using the resident model's actual name.
This enables the26 installed weapon clips to be exercised against their own
skeletons, attachments and material owners. The later explicit paused review
transaction replaces live presentation resources as documented in
[pause and costume switching](pause-and-costume-switching.md); neither admission
route implies combat weapon switching or ammunition acceptance.

Admission publishes costume/class only after both model uploads succeed.
Repeated identical requests are harmless; changing either admitted identity
rejects. Costume transactions retain the selected weapon unchanged. Cancellation
retires the owners and clears both identities. The actual-content fixture covers
all four catalogs, idempotence, rejection, failed upload rollback/retry and fresh
admission after cancellation. Native per-class animation/action captures remain
a separate validation record.

Both C++ MCP corpora were consulted for design and implementation: Core R.20
(#rr-owner) preserves explicit resource ownership; performance COPY.5
(`guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md`) preserves
unique device ownership and safe retirement rather than copying handles. The
existing RAII admission scope is reused; no new per-frame ownership or allocation
mechanism was added.

Initial weapon admission verification passed: the original-root corpus admitted
all four resident weapon owners and26 prepared clips, retained identity on
repeated calls, rejected live class changes, rolled back an injected upload
failure, and admitted a new class after cancellation. The complete run ended
with zero live domain bytes. Evidence: `weapon-admission-corpus.log` and
`weapon-admission-build.log`; fatal scoped tidy passed both mission loading and
the corpus includer. These data/lifetime checks do not replace the native
per-class presentation review.
