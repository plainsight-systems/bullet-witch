# Alicia animation layers and weapon clocks

This contract distinguishes recovered pose composition from game action policy.
It extends [animation coverage](alicia-animation-coverage.md) and the
[prepared catalog](prepared-animation-catalog.md). Native action-family playback
is not established merely by preparing every clip.

## Recovered local-pose order

`0x423d20` owns two arrays of 32 channel roots: ordinary at runtime `+0x120`,
additive at `+0x1a0`. Channels are visited in increasing order. Each controller
chain is flattened by `0x42b890`, then evaluated oldest to newest. The oldest
controller gets weight one; later contributors retain their own blend weight.
The helper deactivates obscured older controllers when a newer weight reaches
one. This lifetime operation belongs to playback, not the pose sampler.

`0x41d920` returns the per-bone channel word at motion header plus
`24 + 4 * bone_count`. Bit `channel` admits that bone. This is authored data,
not a guessed upper-body subtree. Bone-to-track mapping is a separate table.
Optional retarget metadata (`0x41eda0`) remaps bones and translation scales;
current compatible Alicia skeletons do not need that separate retarget path.

For each admitted ordinary bone, `0x425540` blends translation/scale linearly
and rotation with shortest-path quaternion interpolation. Each higher channel
starts a new chain, replacing lower-channel values on its admitted bones. The
first admitted contribution normally copies at weight one. If an older chain
member excludes the bone, the first admitted newer member may have weight below
one: the original copies its rotation but **adds weighted translation and scale**
to the prior buffer. Preserve this unusual branch rather than replacing it with
an assumed normalized weighted sum. Missing ordinary tracks use skeleton bind
values. A bone with no admitted contribution uses bind values in the portable
contract; live original behavior without any base channel is not established.

Additive channels are sampled separately. An absent track contributes nothing.
The first admitted sample copies its local Q/T/S; subsequent samples compose
Hamilton `sample * accumulated`, multiply scales and add translations. The
result applies to the ordinary pose as Hamilton `additive * ordinary`, scale
product and translation sum. Coefficient tables `0x827f60/70/80` establish the
quaternion sign/order. Although chain flattening produces weights, the additive
loop does not use them. Do not introduce reference-pose subtraction, weighted
additive interpolation, or filename-derived mode. In the shared row-matrix
convention this product applies the ordinary transform's rotation first.

Composition precedes local matrix conversion, model-local modifiers, hierarchy,
secondary constraints, skin palette and attachment publication. Root displacement
extraction remains the game movement owner's explicit operation. A prepared
sample can lock consumed root translation before layering.

There is an additional source release mechanism: `0x5e1c30 → 0x426de0` allocates
a per-model pose snapshot at `+0x8b4`. While a channel above zero contributes,
`0x423d20` stores the affected bone mask and resulting local pose, weight one.
When higher channels disappear it decreases the weight by `3 * dt` and blends
those saved bones back toward the current pose. This one-third-second release
is separate from live controller-chain blending. It must be connected explicitly
by the playback owner; the stateless layer evaluator does not invent a clock.

## Body and weapon commands

| Consumer | Observed command and ownership |
|---|---|
| Runner base | Model motion owner `+0xa10`, ordinary channel zero through `0x5e4760 → 0x5f9290`. |
| Shooter reload/body receivers | `0x62e570`, `0x62e6d0`, `0x62fa50` call `0x5f9290` with channel one and model controller `+0x5360`; weapon-class bases `0x150/170/190/1b0` or `0x160/180/1a0/1c0` combine the request's low directional nibble. Blend is 0.3 seconds. |
| Shooter transition cleanup | `0x62e400` can change the class/stance slot by 16 or stop its controller through `0x5e4130`; the latter invokes removal callbacks for its ring before clearing current slot. |
| Weapon activation | `0x615900` constructs the attached model, applies local half-turn, selects default slot zero then requested stance slot one/two, each with 0.3-second blend. |
| Weapon reload | `0x618150` updates weapon state and requests slot three/four from its own model via `0x5e4760`, start zero, fresh true, blend 0.3. The body reload receiver calls this separately from its body-layer request. |
| Weapon catalog | `0x617210` publishes seven slots: default, stand, shoot, stand reload, shoot reload, shoot end, shoot after. Missing publications stay missing. |

Thus body/weapon synchronization is a coordinated **request**, not one normalized
clock or a promise that their durations match. Exact shoot/end/after request
receivers, class/action interruption priorities and all magic/damage family
transitions remain bounded continuations; do not infer them from the table.
`0x41f7d0` only stores a controller field (`+0xc44`), not a channel insertion
operation. Model virtual callbacks own insertion/removal; decompiler parameter
names must not substitute for that call boundary.

## Portable mechanism and reference

`PreparedClip` retains immutable authored masks. `SkeletonPose::evaluate_layers`
accepts ascending channels and oldest-to-newest borrowed samples with explicit
times/weights. It owns neither gameplay action enums nor event dispatch. It uses
existing pending buffers and publishes only a complete finite pose. Shared
quaternion arithmetic belongs to `bw_math`. The caller supplies already prepared
clips, so no asset lookup or allocation occurs during evaluation. Cost is
proportional to bones times live contributors and their key searches; prepared
masks share clip lifetime across compatible costumes.

Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`AnimNode_LayeredBoneBlend.cpp::Update_AnyThread/Evaluate_AnyThread`: evaluate
child poses, retain prepared bone ownership and separate pose blending from
node timing. Adopt this separation, but preserve the original channel order,
local-space equations and masks. Unreal's mesh-space options, normalized blend
weights and temporary pose allocation are not imported. The existing snapshot
is ignored research evidence; no Unreal implementation is copied.

Both C++ design and implementation consult Core I.1/E.4 and performance
MEM.9/CDSA.32: explicit order and borrowed lifetime, failure-preserving output,
load-time mask preparation, fixed steady-state storage. Evidence exports are
pass103-pose-phase, pass106-animation-layers-weapon and
pass108-layer-weapon-receivers. The original image identity is recorded alongside
those ignored diagnostics. No original executable or asset content is tracked.

## Controller pools and phase boundaries

The pool sizes are recovered rather than chosen globally. Base model constructor
`0x5dfe60` initializes its ordinary owner at `+0xa10`, capacity four, with
`0xc70`-byte entries. Alicia-derived `0x5e0830` adds an ordinary owner at `+0x5360`
with capacity two and an additive owner at `+0x5398` with capacity one. Each uses
its own current index and motion publication table. Therefore the engine must
support more than two overlapping base clips while allowing smaller pools.

`0x5e47a0` selects the next ring entry modulo the owner's capacity. Before reuse,
its virtual removal callback removes that exact entry from the old channel;
after initialization its insertion callback publishes it at the new channel
head. Ordinary callbacks lead through `0x5e1990/0x5e1d80` to
`0x425400/0x427060`, additive callbacks through `0x5e1980/0x5e1d70` to
`0x4252f0/0x426fd0`. Links identify both neighbors; removal splices the node and
updates the channel head when necessary. This is a retained bounded pool, not a
new heap allocation per transition. Reusing a still-contributing old ring entry
is a defined source interruption, not permission to grow the pool.

Controller clock `0x41e440` decreases remaining blend seconds by unscaled game
delta, clamped at zero, then advances clip frames by delta times its own effective
frame rate at `+0xc34`. `0x41f800` changes only that controller's effective rate
from its original rate at `+0xc30`. `0x41e4c0` computes blend weight as
`slope * remaining_seconds + final_weight`; the ordinary incoming transition
starts at zero and approaches one, while the first live controller starts at one.
The original `0x41f0a0` stores previous/current frame, marks boundary crossings,
wraps repeating clocks at the exact end and clamps nonrepeating clocks. Signed
playback is supported by this helper; root extraction/actor movement must state
its own signed-time policy.

`0x5e1a00` retires entries older than the newest controller with full weight.
It also has an explicit keep-current-terminal-pose input: the primary model
owner keeps it, while later registered owners can clear their entire pool when
the current nonrepeating clip finishes. `0x423b40`, after pose evaluation,
separately unlinks finished controllers whose auto-removal flag is set. These
are different policies and phases; do not silently make every completed clip
return to idle or retire all outgoing contributors when a new request arrives.
Source exports pass111 and pass112 retain the exact callbacks and fields.

The generic layer implementation is now in `pose.hpp/.cpp`: authored mask copy,
ordered local blending/additive composition, shared Hamilton product and pending
pose publication. In-process verification covers masked lower-body retention,
three-component additive composition with noncommuting rotations, the unusual
masked first-member branch, absent additive tracks, no steady-state allocation,
and invalid-request rollback. The source-backed `PlaybackRing` now owns caller-sized retained controllers,
independent signed clip clocks and unscaled blend clocks. It publishes borrowed
ordered layer spans and reports overwritten generations for event interruption.
Post-clock obscured-entry retirement and post-pose completion retirement remain
distinct calls; explicit owner clearing/hold policy stays with the game adapter.
It allocates its entries/contribution storage once and validates complete ticks
before mutation. Start time is explicit seconds; original negative normalized-
phase sentinels must be resolved by the compatibility caller. `PoseReleaseState` now supplies the optional higher-layer release snapshot; game
action/weapon transition integration remains separate work.


## Runner root movement is not the blended pose root

Instruction evidence in `0x5e5280` distinguishes the two outputs. At `0x5e52e2`
the current base ring index is compared with `model+0xa2c`; the conditional move
selects `model+0x4a80` only for that entry, and `0x41f840` stores this output pointer
at controller `+0xc24`. All other base entries receive a null output pointer.
`0x428ab0` samples each admitted controller's previous/current root transform,
handles the wrap endpoint, and writes its individual relative transform through
`0x41ed60 → 0x422460` before calling `0x425540` to aggregate the chain. The aggregate
also has an output, but this caller supplies a temporary stack matrix and does
not consume it afterward. Runner `0x664be0` reads `model+0x4a80` for movement.

Consequently this consumer uses the **current base controller's own extracted
root**, not midpoint-weighted outgoing/incoming displacement or a generalized
sum of the live pose chain. Its direction-reference query `0x5e2060` independently
selects that same current controller. Pose overlap and root movement ownership
are separate contracts. The selected traversal clips' identity root rotations
permit a translation delta. The entire catalog must not silently inherit that
restriction: the source uses a relative transform operation with rotation and
per-controller modifier scale, so other actions require their actual consumer.
Evidence is pass94/pass112 instructions and the existing Runner export; no new
broad decompilation was needed.

## Higher ordinary channel release snapshot

The optional source allocation `0x426de0` creates one local-transform slot per
bone. In `0x423d20`, any nonempty ordinary channel above zero sets the higher-layer
presence flag. A separate bone mask records bones actually admitted by those
channels. After ordinary and additive composition, a present higher layer resets
release weight to one and copies those masked **local** transforms and the mask.
This snapshot is refreshed every active frame, before model pose modifiers and
hierarchy composition.

When higher channels disappear, the same phase first subtracts `3 * model_delta`
from the saved weight, clamping at zero, then blends current locals toward saved
locals for saved-mask bones: translation/scale linear interpolation and rotation
slerp. It is not continued sampling of the outgoing clip, a world-space blend, or
a blanket whole-body fade. Source state is independent of controller removal and
has no gameplay action identity. A portable implementation can retain one
construction-sized snapshot/mask per pose owner and publish it only when pose
evaluation succeeds; source decay rate belongs in the caller's explicit policy.


Main model update `0x5e3680` calls clock advance `0x42aec0` at `0x5e3722`,
then `0x5e5280` at `0x5e3730`, then pose evaluation and post-pose removal.
Thus root extraction reads the newly advanced previous/current interval and
obscured-entry retirement uses the newly advanced blend weight. PlaybackRing
preserves that main-model ordering. `0x5e2060 → 0x42a5b0` samples the current
controller's current frame for the direction reference; traversal's cached
reference remains valid only because the selected clips passed the constant
reference-track guard.


The relative-root matrix equation is `current * inverse(previous)` under the
recovered row-vector convention. `0x422460` copies/inverts its second argument
with `0x4094b0`, then postmultiplies its third argument by that inverse using
`0x406690`. `0x428ab0` supplies the previous sample as the second argument and
current sample as the third, then scales the extracted translation by the
controller modifier. Its `0x41dac0` call receives the controller, so offset `+0xc`
is **previous frame**, not the motion-resource FPS field at the same offset.

Implemented release state retains constructor-sized saved and pending local
transforms/masks. Invalid steps or failed hierarchy/modifier publication leave
both the displayed pose and saved release state intact. Tests cover a masked
upper pose including additive rotation/scale, a changed actor world transform,
pre-modifier snapshot capture, decay-before-application, rejected refresh,
explicit reset and no allocation after construction. Playback tests cover four
overlapping controllers, reuse/interruption identity, independent rates, alias
requests, signed wraps, post-clock obscured retirement and post-pose terminal
retirement. Targeted resource tests and pinned fatal lint pass. Native game
integration and full repository gates belong to the enclosing actor slice;
these tests do not assert visible action-family acceptance.

### Full-catalog root orientation consumer

The complete 161-clip current-to-previous root census found rotation in exactly
four raised-turn motions: `shoot_turn_le_l`, `shoot_turn_ri_l` and their crouched
variants. Maximum per-frame matrix deviation is about 0.0351191, including small
non-yaw terms (0.000346541). The 17 traversal clips retain identity root rotation
within the diagnostic threshold. Evidence is `actor/root-rotation-census.log`,
with a production prepared-clip probe and reproducible build command.

The recovered Runner root consumer also postmultiplies model-local orientation
by the relative root rotation after translating through the preceding basis.
The full-animation consumer therefore retains a complete orientation matrix;
its yaw is only a derived camera/diagnostic value. Flattening these four clips to
yaw or returning translation alone would discard source input. Ordinary facing
requests may replace orientation; root-driven turn steps compose it. Rendering
still applies the separate original half-turn exactly once. Costume appearance
replacement preserves the existing model matrix and physical owner.


The render-pose removal is equally important: model initialization `0x42a8f0`
sets `+0x90` to one. The final conversion in `0x423d20` then writes a complete
identity matrix for the root instead of converting its composed local SRT;
remaining bones retain their composed locals. This occurs after the outgoing
higher-channel snapshot. Merely locking translation would apply the four turn
clips' rotation both to the physical model and to the displayed hierarchy.
`RootPosePolicy::identity` now expresses that consumer explicitly, removes the
prepared hierarchy root after layer/release/basis work and before hierarchy
composition, and allocates no storage while evaluating. The generic sampler's
separate translation-lock policy remains available. Actual Alicia's hierarchy
root is bone zero; the engine resolves root identity from the skeleton rather
than assuming every imported skeleton uses that storage order.

Source action start offsets are frame units: `0x5e47a0` forwards the argument to
`0x41f2f0`/`0x41f200`, which clamp against authored start/end frames. Action plans
now distinguish explicit frames from seconds and phase preservation; the actor
converts a frame request using the selected clip's FPS. Common magic's literal
one therefore starts at frame one, correcting the earlier second-one reading.
The original native family image at second one is historical presentation
coverage, not verification of this corrected entry phase.
