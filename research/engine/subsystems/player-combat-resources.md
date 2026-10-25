# Player combat resources

October 25, 2026. Source reconstruction for live player resources and HUD
publication. This is separate from permanent Record progression and animation
presentation. Original exports remain ignored under
`research/evidence/reconstruction/player-resources/`.

## Authority and observations

The original player retains separate Damager (`+0x1228`), MagicUser (`+0x122c`)
and four weapon objects (`+0x1254..0x1260`). Weapon presentation/model residency
does not own ammunition. The reconstructed mission resource owner must retain
all four ammunition/cooldown states across selection and costume changes.
`MissionPlayerState` remains the sole authority for effective progression,
selection and spell restriction masks. The resource owner borrows it read-only;
HUD receives copied observations and action code receives a restricted command
capability. Neither receives resource setters or permanent progression mutation.

| Resource | Original owner fields | Meaning |
| --- | --- | --- |
| Health | Damager `+0x34`, `+0x38`, `+0x3c` | Current, recoverable ceiling, maximum |
| Health recovery | `+0x40/44/48`, `+0x4c/50/54` | Current recovery rate/countdown/delay; ceiling recovery rate/countdown/delay |
| Magic | MagicUser `+0x24`, `+0x28`, `+0x2c` | Current, recoverable ceiling, minimum ceiling |
| Weapon | weapon `+0xa4` bits 16–25; `+0xd0` | Ammunition and shot cooldown |

The HUD producer `0x602b70` publishes both health lanes divided by maximum,
and both magic lanes divided by the original constant 100. The second lane is
not merely UI smoothing: the resource owner owns a distinct recoverable pool.
The HUD's own delayed visual trails are another responsibility.

## Initialization and progression

Player constructor `0x5fe970` initializes all three health fields from
`witch/general/setup +0xac` (installed value 5000). Magic current, ceiling and
floor initialize from WA0's full recharge cost (installed value 20), not 100.
Weapon constructor `0x615600` initializes each object's full ammunition from
its own specification. Learned/completed-game progression does not imply full
magic or maximum upgrades.

`0x602390` initializes both stat levels to zero. The constructor invokes
`0x604070`, which installs health recovery from `witch/actor/pw0/body` using
that initial stat level. Its sole recovered code caller is the constructor;
later progression application `0x603cc0` writes levels but does not invoke
that refresh. Do not silently invent live health-stat refresh on purchase.
Magic recovery `0x602b50` reads the current effective stat-one level each update.
Power-up transactions alter permanent Record; current mission progression is a
separate source publication, not an automatic mirrored write.

Body settings contain four entries for levels 0–3:

| Setting | Offset | Installed entries |
| --- | --- | --- |
| Health regeneration enabled | `+0x15c` | 1 |
| Current health full-recovery seconds | `+0x160` | 20, 15, 12, 10 |
| Current health recovery delay | `+0x170` | 2, 1.6, 1.2, 1 |
| Recoverable health full-recovery seconds | `+0x180` | 180, 120, 90, 60 |
| Recoverable health recovery delay | `+0x190` | 6, 5, 4, 3 |
| Magic full-recovery seconds | `+0x1a0` | 60, 50, 40, 30 |

The health installer uses zero rate when full-recovery seconds is nonpositive;
otherwise it stores its reciprocal. Ordinary Damager update `0x66e9c0` first
recovers the ceiling toward maximum, then current health toward that ceiling.
Each rate is multiplied by maximum health. A countdown crossing zero admits
the complete current interval, rather than only the fractional remainder.
Special states with a different Damager dispatch must not inherit ordinary
recovery without tracing their registration.

Checkpoint restore `0x43c1e0` restores a positive health value to both current
and recoverable health through `0x604010`, and positive magic to current and
ceiling through `0x604330`. Zero values skip those restore operations. Maximum
health and magic floor remain initialized. This bounded resource restoration
does not establish world checkpoint restoration or permission to restore a
campaign checkpoint during concept entry.

## Fire and recharge

The installed base specifications `witch/weapon/waN/00_spec` provide:

| Weapon | Rate (`+0x90`) | Capacity (`+0x94`) | Full recharge cost (`+0x60`) |
| --- | --- | --- | --- |
| Machine gun | 10 | 80 | 20 |
| Shotgun | 1 | 16 | 40 |
| Cannon | 1.8 | 24 | 40 |
| Gatling | 60 | 400 | 40 |

`0x616530` returns 1 while cooldown is positive, 2 on ammunition exhaustion,
and 0 after acceptance. Each projectile consumes one round and adds
`1/rate` to cooldown. A negative previous interval remainder can cause several
shots; ammo exhaustion can return 2 even after emitting earlier shots in the
same request. The resource result must preserve both status and emitted count.
Shooter `0x630400` submits fire/recoil presentation only for status zero. For
status two it requests automatic recharge through `0x6309e0`, including the
case where earlier catch-up shots were emitted. Cooldown status is inert.
The child update `0x617a30` subtracts world delta only if old cooldown was
positive, otherwise stores zero. Preserve one negative overshoot; do not
accumulate idle debt or clamp every subtraction.

Shooter `0x6309e0` calculates missing fraction `1-ammunition/capacity`, then
multiplies it by full recharge cost. `0x600200` compares against current magic.
The accepted recharge message carries weapon identity and the calculated cost.
MagicUser receiver `0x60ab20` subtracts the cost from current magic, clamped at
zero; it does not reduce the recoverable ceiling. Weapon receiver `0x618150`
fills ammunition and clears cooldown before requesting the reload animation.
Do not defer resource publication until the animation completes or refill when
a rejected request merely selects a preview pose.

## Spell cost and recovery

`0x613b40` resolves nine spell identities; helper `0x60b220` resolves `01..03`
for purchasable spells and `01` for story spells. The first two floats are
current-magic cost and recoverable-ceiling debit. The original compatibility
paths include `raven_panice` and `tornade`; public names remain readable.

| Spell | Current cost, levels 1–3 where present | Ceiling debit |
| --- | --- | --- |
| Elemental Shot | 40, 40, 40 | 10 |
| Ancient Wall | 20, 15, 10 | 0 |
| Rose Bomb | 35, 35, 35 | 15 |
| Will Power | 30, 30, 30 | 10 |
| Sacrifice | 10, 10, 10 | 0 |
| Raven Panic | 40, 40, 40 | 20 |
| Thunder | 80 | 60 |
| Tornado | 90 | 70 |
| Meteor | 100 | 80 |

`0x613f10` subtracts the first float from current magic with zero floor and
the second from its recoverable ceiling with the initialized minimum floor.
The exact action phase that calls it is spell-owned. Selection, affordability,
action acceptance and cost commit are distinct; this owner does not manufacture
successful target acquisition, projectile creation or spell completion.

Ordinary magic update `0x60aec0` increases current magic by
`(100 / recovery_seconds + temporary_bonus) * world_delta`, capped at its
recoverable ceiling. The optional bonus has a separately retained duration.
Pickup receiver `0x60aa70` increases the recoverable ceiling by its pickup value
times a retained multiplier, capped at 100. Merely waiting does not increase
that ceiling. Missing pickup/world producers must remain explicit.

## Integration and verification obligations

Resource commands belong to an admitted mission frame. Parent action requests
precede child cooldown/recovery publication; animation substeps must not tick
resources again. The resource owner and clock identity are stable, noncopyable
and nonmovable. Their lifetime exceeds borrowed action/HUD capabilities. Paused,
duplicate and foreign frame handling must preserve ownership and resource state.

The independent source image is the established unpacked Steam executable;
Ghidra exports use `-readOnly -noanalysis`. Direct PE reads and CPR/CSTM reads
do not modify the game installation. Settings observations are retained in
ignored `settings.json`. This report describes traced mechanisms; production
implementation and verification results are recorded separately when completed.

Design consultation read Core I.1 (`ri-explicit`) and E.5 (`re-invariant`) for
explicit mutation/initialization contracts, and performance MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) for fixed state and
allocation-free gameplay updates. Implementation consultation separately queried
both corpora and read Core E.4 (`re-design-invariants`) and MEM.9. These are
design guidance, not evidence that ownership or native behavior is correct.

## Implemented resource boundary

`PlayerResources` owns these pools for the mission lifetime. Its settings parse
the original customization tree. It borrows immutable effective progression
and clock identity; it cannot select equipment or mutate the permanent profile.
`PlayerActionResources` only permits fire, recharge and spell cost transactions.
It cannot open/advance frames. The coordinator brackets each admitted frame with
`begin_frame` and `finish_frame`; duplicate, foreign and reentrant frame requests
reject before writes. Paused frames close without resource changes. Read-only
spell affordability remains available to paused UI.

The copied snapshot includes a per-frame recharge receipt bit for each weapon.
Only a committed recharge sets it. A valid next `begin_frame` clears receipts,
including during pause; `finish_frame` retains them for HUD publication.
Rejected frame requests cannot erase pending receipts. The HUD never infers
recharge from an ammunition increase or mutates a receipt itself.

The source maximum magic is 100, but the initialized current/ceiling/floor remain
WA0's authored recharge cost. Completed learning is not a resource grant.
Positive checkpoint values restore both lanes only when mission progression
origin is checkpoint; concept entry discards them. Malformed values exceeding
the decoded maximum reject at the reconstruction boundary instead of importing
invalid state. This is resource initialization only, not world restore.

Implemented normal recovery excludes temporary pickup/buff producers, damage
dispatch, enchanted weapon specifications and nonordinary Damager state
transitions. Those require their actual gameplay owners. The resource type has
no generic setters to simulate them. Projectile effects/hits and spell emission
remain distinct from admitting source action presentation and spending its cost.
Affordability is checked by the transaction; a delayed admission/commit split
must preserve the source spell's actual cost phase rather than manufacture a
completion event.

## Verification

An ignored Unicorn witness executes original x86 `0x613f10`, `0x60ab20`, and
`0x60aec0` (including original getter `0x602b50`) against synthetic owner
storage. No function hooks or rewritten algorithm stand in for these routines.
It confirms two spell debit lanes, the recoverable minimum floor, recharge's
current-only debit, and effective-stat magic recovery. The witness script and
JSON are under `research/evidence/reconstruction/player-resources/`; the image
SHA-256 is `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
This is a bounded native arithmetic witness, not a complete gameplay execution.

The production settings decoder was also run against the installed CSTM record
and matched the health, four weapon specifications and nine spell costs listed
above. The independent reads did not alter the game installation.

Six focused in-process resource cases cover initialization/recharge lanes,
per-weapon persistence and exhaustion after partial catch-up, checkpoint/cast
recovery, pause/frame ownership, malformed settings, and spent-ceiling limits.
The isolated binary passes strict compiler warnings. Both authored translation
units pass pinned clang-tidy with fatal diagnostics and repository configuration.
The parent slice's integrated build, full lint and runtime acceptance remain
separate gates; these focused checks do not substitute for them.
