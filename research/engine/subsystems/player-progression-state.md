# Player progression, mission state and persistence

October 24, 2026. Read-only audit of current implementation and existing local
original-code witnesses, followed by this authored note. No game files were
modified. This is a dependency map for the next slice, not implementation or a
claim of complete save compatibility.

## Status of this audit

This note preserves the pre-implementation dependency audit. Its missing-contract
and unresolved tables below are historical. Subsequent original-code work is in
[progression rules](player-progression-rules.md) and
[concept entry](concept-progression-entry.md); the implemented ownership and
remaining limits are in [player progression architecture](../../../docs/architecture/player-progression.md).
Typed profile operations, mission effective levels and weapon admission now exist.
Full world save restoration, result producers, story grants and HUD/casting remain
separate work; refer to those current documents rather than the audit's old gaps.

## The selected mission is not a fresh campaign

The original Steam front end gates concept missions on achievement 13, awarded
for completing the game. The reconstruction intentionally removes that menu
gate. This does not establish what effective player progression the original
concept entry supplies. See [front-end policy](front-end-menu-policy.md) and
[profile persistence](profile-persistence.md).

The user expects the concept missions to provide the weapons and spells available
after completion. **Do not apply fresh-campaign restrictions to One Shot Kill.**
Preserve the accepted traversal's weapon availability while tracing whether
original concept entry inherits the completed profile, grants capabilities, or
overrides levels. Completion alone does not establish that every upgrade is at
its maximum. The gate removal also does not authorize fabricated achievements or
an invented all-max progression record.

The source path to resolve is selected concept routing, original root
`concept_mission()`, mission setup/player construction, progression application,
and mission-specific level/mask commands. An inventory of installed assets or
loaded animations cannot answer this policy question.

## Current implementation

| Responsibility | Implemented behavior | Missing contract |
| --- | --- | --- |
| [Profile](../../../src/game/profile.hpp), [implementation](../../../src/game/profile.cpp) | Owns 72-byte System and 992-byte Record payloads; preserves opaque bytes; exposes settings, difficulty, costume/content availability and checkpoint metadata | Typed gameplay progression, upgrade points and purchase rules |
| Checkpoint input | Retains a validated 307,200-byte checkpoint file as read-only bytes | Gameplay capture, serialization and live restoration |
| [Weapon selection](../../../src/framework/startup_application.cpp) | Cycles `(current_weapon_class() + 1) % 4` in the accepted presentation scene | Effective availability derived from the original mission/progression policy |
| [Weapon replacement and actions](../../../src/game/stage_animation_review.cpp) | Validates class, grounded/action state and resource publication; presents fire/reload/melee | Ammo mutation, projectiles, hits, damage and gameplay admission |
| [Menu gates](../../../src/game/menu_flow.hpp) | Keeps campaign difficulty/stage gates separate from installed content and the approved concept-menu exception | Complete local progression production/import |

The current presentation deliberately exposes all four weapon models. Its fire
and reload paths explicitly report no ammunition/projectile outcomes. This is
accepted animation-review functionality, not evidence that campaign progression
or original concept-entry policy is implemented.

Steam content refresh operation 2300 makes supplied costumes and extra mission
packages available. That is distinct from weapon/spell acquisition, upgrade
levels and achievement gates. Do not generalize the costume refresh to abilities.

## Original evidence already available

These findings come from existing Ghidra exports of the Steam executable.
Recovered pseudocode has inferred types and can omit calling-convention context;
implementation must check the relevant instructions and full callers rather
than copy these expressions into production.

| Witness | Established observation | Boundary |
| --- | --- | --- |
| `0x4bb800` | Fresh Record defaults include two stat bytes `1,1`, four weapon bytes `1,0,0,0`, and nine magic bytes `0,1,0,0,1,0,0,0,0` | These are fresh-profile defaults, not established concept-mission effective state. Do not assign spell names from byte positions alone. |
| `0x43c1e0` | The fresh-player branch applies persistent Record values through `0x603cc0`. A restore branch compares two stat, four weapon and nine magic bytes and raises each checkpoint byte to the corresponding Record value when greater; it also restores current health/magic/weapon and serialized world/callback sections | Full branch eligibility and mode-specific callers must be traced before defining concept entry or a general merge policy. |
| `0x603cc0` | Applies player stat and weapon progression; weapon level is held in the low nibble. Ensures an available weapon and repairs selection to an available one; delegates magic values to `0x604140` | Do not infer the complete upgrade cost/award model or all gameplay gates from this setter. |
| `0x43c520` | Captures current player health/magic, selected weapon, stat/weapon/magic values and mission state, then invokes base and mission serialization | Snapshot state is distinct from settings and permanent progression; nested world formats remain incomplete. |
| `0x493a20`, `0x4932c0`, `0x494020` | Write/read bounded base and mission sections; the mission writer appends background, enemy and callback sections | The file's fixed size is distinct from its used length; full object restoration is not implemented. |
| `0x47c300` (`_SetMagicMask`) | Stores the script's mask at mission owner `+0x61484`; One Shot Kill supplies `8 + 64 + 128 + 256` | Consumer, bit meaning and polarity remain untraced here. This must not be conflated with learned spell levels. |
| `0x47c280` (`_SetMagicLevel`) | Provides a separate native route into mission magic values and the player magic-level setter | Its existence does not prove that One Shot Kill grants all abilities or maximum upgrades. |

The current `Profile` constructor already preserves the fresh default bytes at
Record offsets `+2..+16`, but there are no typed gameplay consumers for them.
Retaining bytes is not implementing progression.

Original root Lua distinguishes the campaign and concept result paths. Campaign
completion invokes results, initializes mission data, advances the stage, then
invokes the power-up and save flows between stages. `concept_mission()` invokes
results and returns to the main menu; it does not run that campaign power-up
sequence. Mission startup also has real score/player/cockpit/callback setup gates
before unchanged `start()` can run; see
[mission-start admission](mission-start-admission.md).

## Evidence locations

Raw exports and recovered scripts below are ignored local evidence, unavailable
in a public clone. They are references, not project source imports.

- Fresh defaults: [0x4bb800 export](../../evidence/reconstruction/profile/export3/functions/004bb800.c)
  and [instructions](../../evidence/reconstruction/profile/export3/instructions/004bb800.txt).
- Restore/merge: [0x43c1e0 export](../../evidence/engine/pass-06k/functions/0043c1e0.c)
  and [instructions](../../evidence/engine/pass-06k/instructions/0043c1e0.txt).
- Snapshot: [0x43c520 export](../../evidence/engine/pass-06k/functions/0043c520.c)
  and [instructions](../../evidence/engine/pass-06k/instructions/0043c520.txt).
- Progression application: [0x603cc0 export](../../evidence/engine/pass-70-player-weapon-bindings/functions/00603cc0.c).
- Magic presence and levels: `research/evidence/native-decompilation/functions/`
  contains `006139d0.c`, `00604140.c`, `0047c280.c` and `0047c300.c`.
- Serialization: `research/evidence/engine/pass-06k/functions/` contains
  `00493a20.c`, `004932c0.c` and `00494020.c`; authored format summary in
  [saves and progression](saves-and-progression.md).
- Script flow: `research/extracted/scripts-recovered/` contains
  `root.lub--42c4bebad62b.lua` and `cm01_00.lub--105c3988e33f.lua`.

## Ownership and next dependencies

The following are proposed responsibilities, not recovered class names:

1. **Game progression state** owns established permanent capabilities, upgrade
   levels, points and completion records. Save encoding adapts that state; raw
   bytes are not the gameplay API.
2. **Mission/player runtime state** owns current health, magic, ammunition,
   selected weapon, temporary restrictions and action admission. Effective
   availability follows the original mode-entry and mission rules. The HUD reads
   these owners rather than maintaining another state copy.
3. **Checkpoint snapshots** capture supported player/mission/world state at the
   recovered phase, with explicit restoration and merge rules.
4. **Platform storage** owns file bytes and I/O outcomes, not progression policy.

Establish these state/default/restore contracts alongside world time, input and
pause ownership, before binding a gameplay HUD or declaring combat admission.
Map concept entry first for the selected One Shot Kill slice. Add genuine entity
startup and mission consumers, then complete checkpoint/outcome persistence.
Upgrade purchase and save/reload behavior require their own traced producers and
consumers before progression support is claimed.

Still unresolved: exact upgrade currency fields and award/cost rules; complete
spell identity/level semantics; mission-mask consumers; concept inheritance versus
grants/overrides; full checkpoint schema and restoration. The accepted traversal
availability remains unchanged while these are resolved. No additional shim or
fresh-campaign restriction is authorized by this note.

## Verification

Read-only implementation and existing-source inspection only. No runtime tests,
game execution, save modifications or gameplay implementation were performed.
