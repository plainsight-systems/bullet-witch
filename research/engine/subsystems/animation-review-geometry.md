# Animation review pose compatibility

The selectable animation geometry failure has a concrete reproduction:
`shoot_stand_fire_add` selected as a body clip collapses Alicia into a pile at
ground level. The same resource applied as an additive delta over an ordinary
pose preserves her proportions. Conversely, selecting ordinary `stand_l` as an
additive clip doubles skeletal offsets and visibly stretches the character.
The review interface admitted incompatible pose representations; these
witnesses do not support changing the source sampler or adding compensating
bone transforms.

## Source representation contract

Ordinary local poses and additive local deltas share the original motion
container. The receiver chooses their interpretation. `423d20` samples the
ordinary chain; the additive path composes quaternion products, scale products
and translation sums. It does not subtract a reference pose. See
[ordered layers](alicia-animation-layers.md). Therefore a common decoded catalog
does not make every clip interchangeable between body, upper and additive banks.

Accepted shot `630400` sends these native slot families to additive receiver
`5e47a0`: `0xd0`, `0xe0`, `0xf0`, `0x100`, `0x110`, `0x120`, plus airborne slot
`0x2c1`. The low nibble comes from the current body slot. Damage receiver
`6867b0` independently requests additive slot `0x394`. Resolving those slots
through the actual ordered alias catalog identifies these 16 installed resources:

| Consumer | Resources |
| --- | --- |
| Damage | `dm01_add` |
| Cannon dash | `shoot_dash_ba_cannon_add`, `shoot_dash_fr_cannon_add`, `shoot_dash_le_cannon_add`, `shoot_dash_ri_cannon_add` |
| Ordinary dash recoil | `shoot_dash_vib_ba_add`, `shoot_dash_vib_fr_add`, `shoot_dash_vib_le_add`, `shoot_dash_vib_ri_add` |
| Crouched recoil | `shoot_squat_vib_add`, `shoot_squat_walk_vib_fr_add`, `shoot_squat_walk_vib_ri_add` |
| Cannon walking | `shoot_walk_fb_cannon_add`, `shoot_walk_lr_cannon_add` |
| Ordinary walking recoil | `shoot_walk_vib_fr_add`, `shoot_walk_vib_ri_add` |

The separate, unaliased `shoot_stand_fire_add` has a directly witnessed delta
representation: ordinary publication collapses the skeleton, whereas additive
publication retains the ordinary body proportions. Its original action caller
remains unresolved. An exact compatibility entry classifies its representation
for review without inventing a gameplay action. No generic filename-suffix
heuristic is used.

`CharacterAnimations` prepares `MotionPoseSemantics` beside its immutable
catalog, defaulting every entry to **unknown**. The installed Alicia census is
135 ordinary, 17 additive and 9 unknown. Known actors do not grant a package-wide
ordinary default; a name in the compatibility publication table is insufficient. Source aliases, shared repeat writes and
parameter identity remain unchanged. Metadata costs one byte per clip, is
prepared during loading, and is borrowed by review admission and menu filtering.
This is a pose-representation boundary, not evidence that arbitrary action
transitions or event outcomes are supported. Core I.1 and performance CDSA.32
were queried/read for design and implementation; explicit metadata prevents
implicit mode mistakes and avoids repeated source lookup during selection.

## Verified admission and unresolved publications

Ordinary admission is derived from these recovered receiver contracts. The
classifier resolves only matching canonical slot/publication pairs; a caller
remapping a known slot to another resource cannot grant that resource admission.
Unknown actors remain unknown even when their resource is named `stand_l`.

| Verified consumer | Admitted native slots | Evidence |
| --- | --- | --- |
| Standing/dashing/crouched Runner | Families 0,16,144 with nibbles 0,1,8–14 | `6667e0` ground selection and the reconstructed `select_ground_motion` |
| Shooter switch/reload/discharge/raise/lower | `0x130..0x28f` | `630400`, `6313b0` and the request receivers recorded in [action policy](alicia-action-policy.md); ordinary upper requests, also mirrored into idle body |
| Scripted jump | 656–704 | Runner start/flight/landing receiver formulas in [scripted jump](scripted-jump-policy.md), independently of hardware evade |
| Close attack, evade, fall | 706–721 | StateGrapple `66bf30`, Dodger `68a900` and Runner fall publication |
| Weighted/crouched death | 730–740 | `687ff0` ordinary body request, [reaction transitions](alicia-action-transitions.md) |
| Directional damage | 749–760 | `687e40` tables and `665880` ordinary body receiver |
| Stay, element, spells | 767,770,771,779,782–789 | `6667e0` stay; `69ca70` sets 770 then common ordinary `613570`; the eight spell owners in `spell_presentation_policy.cpp` and the linked transition research |

The nine unknown installed publications are `die_snip_ba`, `die_snip_fr`,
`die_snip_le`, `die_snip_ri`, `dm_ear`, `magic_teleportation`,
`magic_teleportation_s`, `shoot_stand_fire_l` and `turn_around`. Some have aliases,
but this bounded verified-consumer set does not establish their representation.
They remain visible but disabled in review and must be rejected before changing
the active pose. Unknown does not mean malformed or absent. It means this adapter
has not established safe representation admission. No counts were preserved by
classifying the remaining names as ordinary.

For owners `wa0` through `wa3`, original constructor `617210` → `5e2180`
registers seven ordinary motion publications: `defaultmotion`, `stand_l`,
`shoot_l`, `stand_reload`, `shoot_reload`, `shoot_end`, `shoot_after`.
Only those exact resources are admitted; other weapon publications remain
unknown. The actual installed counts are 6/7/7/6 because absent entries stay absent.

The explicit unbound delta exception above remains a separately documented
representation witness, not a recovered action binding. All other admission is
receiver-backed. The corpus checks both counts and negative identity cases:
unmapped actor with a familiar name, an unknown Alicia resource remapped into a
known standing slot, and an unrelated weapon resource. Each remains unknown.

## Native geometry investigation

The ignored `interactive-animation/catalog-geometry-probe.mm` uses the actual
Stage review owner and native renderer. It advances each selected resource for
its complete authored duration plus a short settling interval at 60 updates per
second. It resets review ownership before each independent selection. The full
baseline sweep exercised all 161 body, 161 upper, 161 additive and six weapon
selections, preserving the invalid combinations as diagnostic evidence.

At six-update intervals and the final update, CPU witnesses skin original body
vertices using the actual completed world bones, original inverse-bind matrices,
material palette mappings and source influence weights. They record body bounds
and maximum parent/child distance, including secondary-bone results. These
measurements do not include facial position deltas or weapon mesh bounds; native
captures include the complete submitted scene. Bounds are an outlier locator,
not a visual acceptance oracle. A long helper/weapon marker can exceed anatomical
bone lengths without deforming the visible body: the inspected upper
`magic_meteorite` outlier is such a case.

Costume5 comparisons are retained under
`interactive-animation/catalog-geometry/`:

- `sample-8-shoot_stand_fire_add.png`: delta wrongly used as body; collapsed mesh.
- `sample-9-shoot_stand_fire_add.png`: delta wrongly used as upper; collapsed torso.
- `sample-10-stand_l.png`: ordinary pose wrongly used as additive; stretched body.
- `sample-10-shoot_stand_fire_add.png`: the delta in its compatible bank.
- `sample-8-stand_l.png`: ordinary base control.

The `catalog-geometry-full/metrics.tsv` and native outlier captures preserve the
full-duration baseline. In the stand comparison, maximum parent/child distance
changes from approximately1.20 to2.40 when the ordinary pose is incorrectly added.
The valid delta control retains approximately1.20. This investigation identifies
a demonstrated review-admission defect; it does not claim all resource names
have recovered gameplay state owners or every possible layer combination has
original-game visual parity.

A separate weapon-only pass follows all four installed catalogs through complete
resource durations: 6/7/7/6 entries, 26 selections total. It evaluates a weapon
mesh reference from the actual current weapon clip/time and completed body
attachment, retaining native midpoint captures from the production owner. The
reference does not model the outgoing visual blend during entry, so it is not
claimed as a bitwise observation of every intermediate GPU palette. Across the
sweep, reference bounding-box diagonals remain 2.141–2.145m (Gunrod),
1.947–2.046m (Shotgun), 2.207–2.527m (Cannon), and 2.235–2.241m (Gatling).
The four inspected reload midpoint captures show coherent weapon geometry;
`catalog-weapon0` through `catalog-weapon3` retain all captures and metrics.
This closes the initial body-only metric limitation without claiming every
resource's original action synchronization is recovered.
