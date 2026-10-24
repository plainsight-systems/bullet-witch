# Prepared animation catalog

The catalog separates immutable motion publications from compatibility aliases
and live controller clocks. The original binding constructor (`0x601250`) visits
slots in order and writes each resolved resource's repeat flag. A later alias
therefore affects every slot sharing that resource: `fall_l` receives nonrepeat
at 721 and repeat at 953. Preserve raw, requested and final repeat policies;
never duplicate keys or invent independent per-alias resource state.

Engine `PreparedClipCatalog` accepts named motion definitions and ordered alias
requests. It resolves names once, preserves unresolved aliases, prepares one
clip per publication and provides indexed immutable access. Game-owned binding
policy supplies source slot/name mappings. Parameter-only and shape publications
remain independently addressable; an absent motion does not erase its parameters.
No runtime executable parsing or proprietary animation payload is embedded.

Compatible costume instances share immutable keys. Absent tracks use the pose
owner's own skeleton bind values, not the skeleton that first prepared the keys.
Game validates name/index/parent compatibility before sharing; the engine does
not infer semantic compatibility from an equal bone count. Actor clocks,
transitions, evaluated poses and secondary state remain per instance.

Reference inspected: Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`AnimNode_SequencePlayer.cpp::Initialize_AnyThread`, `UpdateAssetPlayer` and
`Evaluate_AnyThread`. Sequence assets remain shared while node accumulators and
pose outputs are separate. Adopt that ownership boundary, preserving Bullet
Witch's ordered shared repeat policy rather than importing Unreal's independent
loop option or rate rules. Existing pose preparation/sampling remains the
reconstructed engine implementation.

Loading prepares keys once; action changes and costume replacement must not
repeat decoding or introduce per-frame lookup/allocation. Measure catalog build
cost and retained domain bytes against actual content before claiming acceptance.
Header contracts consult Core I.11/E.4 and performance CDSA.32/MEM.9. Catalog
coverage alone does not implement action transitions, events, layers or combat.

## Current integration evidence

The original-root corpus prepares all 161 body motions, 164 parameter
publications and six shape publications. The compatibility table resolves 415
direct motion slots; missing death motion lookups do not retry the parameter
fallback name. Slots 721/953 share one prepared clip with the final repeated
resource policy. A paused appearance candidate shares the body and weapon
catalog identities and prepares only costume-local evaluated state.

A diagnostic Debug preparation of the actual PW0 support measured 74.15 ms and
18,667,777 retained domain bytes, including prepared event commands. Loading
received 325 progress pulses (161 clips plus 164 parameters). Teardown returned
the domain to its prior value, then zero after source owners retired. This is
one host observation, not a target-independent budget or a full mission load
time. Reproducible commands and output are under ignored
`research/evidence/reconstruction/actor/catalog-cost-*`.

Retained presentation now has an explicit per-draw visibility update contract.
It follows the source command executor's group admission (`0x427220`) while
keeping model/weapon target interpretation in game event receivers. Validate the
whole destination batch, update indexed booleans, and omit hidden draws from both
color and shadow passes; geometry and material bindings stay resident. This
extends the existing retained-output boundary rather than rebuilding world
draws for a motion event. Listener target 1000 is not assumed to mean a body
group.

## Retained actor playback integration

The actor now owns a stable `CharacterMotionPlayer` set with the recovered
body/upper/additive capacities 4/2/1 and a separate weapon controller ring.
Appearance candidates share these clocks and immutable catalogs; they evaluate
new costume-local poses without ticking the shared action owner. Typed parameter
bindings are prepared by clip index, so selection does not search archive names.
The actor assembles ordered ordinary and additive layers, then the source local
reference modifier, secondary constraints, attachment and retained palettes.
Higher-channel release uses a costume-local masked snapshot and source decay
rate; costume replacement preserves a compatible snapshot without allocating
while copying it.

Only the selected controller supplies the motion event timeline. Outgoing clips
retain their visual contribution and independent clocks without duplicating
sound, visibility or facial events. The selected body controller publishes a
raw relative root matrix during the model child update. The following parent
movement consumes that retained interval through the newly current reference;
selection alone does not erase it. Outgoing blend controllers do not supply a
second root contribution. Fresh-identity, selection/reference and paused
appearance witnesses preserve this distinction. The relative transform retains
the observed signed wrap and unit root scale; multi-cycle rotation accumulation
is not extrapolated. See [actor/model phase](actor-model-clock-order.md).

The existing original-root/startup corpus passes after this integration, including
actual catalog identity, directional roots, traversal timing and balanced teardown.
Catalog coverage remains distinct from family acceptance. The integrated
source family policies, facial/visibility/audio receivers and native witnesses
are recorded in [coverage](alicia-animation-coverage.md). Unknown world/effect
receivers remain explicitly unresolved; decoding their records does not create
an effect.
