# Gameplay HUD behavior-ownership review

October 25, 2026. Independent review of the uncommitted slice against accepted
baseline `3e985bc`. **Complete: B1–B6 repaired and independently rechecked; final repository and original-content gates passed.**
This review owns no production code and does not authorize commit or launch.

The user's objection is substantiated. Const borrows, stable lifetimes and
restricted resource handles did not establish appropriate ownership of behavior.
The previous review checked those properties but its broader clearance missed
game policy implemented in the framework and newly added platform policy.

## Findings before correction

Line references below identify the inspected pre-correction working tree.

| ID | Location | Concrete defect | Required repair |
| --- | --- | --- | --- |
| B1 | `src/framework/startup_application.cpp:337`–376 | The host builds learned/permitted spell observations, interprets ring input, consumes reload/melee/weapon/aim commands, and supplies missing-cast diagnostics. These are game selection/action policies; calling their individual helpers does not make this composition-only. | A game action/input owner must combine progression/resource observations, run selection and return admitted player input plus a typed selection/admission result. The host must not interpret spell IDs, affordability, ring face precedence or cast rejection text. |
| B2 | `src/framework/startup_application.cpp:468`–485 | The framework derives cockpit semantics from resource lanes, selected magazine, the recharge bit mask and targeting state, then decides whether HUD state machines may advance. This leaks presentation integration and phase policy into startup. | A game HUD presenter must map read-only observations, retain cockpit lifecycle/visibility and select update/draw eligibility. Framework supplies immutable timing and invokes explicit phases. Presentation-only state must not become writable player state. |
| B3 | `src/platform/macos/gamepad_actions.hpp:20`, `.mm:6`, `startup_host.mm:578` | Newly added `magic_close_chord` and `magic_face_choice` place Bullet Witch's combined-shoulder expiry and face-slot precedence in the target backend. Hardware sampling and logical button edges are platform responsibilities; the ring's meaning is game policy. | Publish neutral face/shoulder held/pressed observations. Put chord/slot interpretation in the game input adapter, including keyboard-equivalent button observations. Do not merely rename the same game rules inside the target. |
| B5 | First repair draft, `src/game/player_cockpit.cpp::show` | A suppressed update/draw returned without submitting anything, leaving the renderer's retained prior sprite list. Resume consumes a non-simulating UI frame, so the previous pause menu could remain visible; hidden activation likewise could not remove stale HUD output. | Separate update eligibility from output retirement; explicitly clear or publish the appropriate list and test the transition through the real output seam. |
| B6 | Repair API, `src/game/mission_interaction.hpp::set_notice` | A new generic `string_view` setter retains caller storage with no declared lifetime. Current call sites use literals, so no dangling runtime case is demonstrated, but the interface admits short-lived text and unrelated mutation. | Prefer a typed status setter with static-text mapping; keep the frame path allocation-free. |
| B4 | `docs/architecture/player-gameplay.md`, `gameplay-hud-independent-review.md`, `docs/process/MEMORY.md` | Documents describe read-only presentation and completed ownership review without accounting for behavioral ownership at the integration point. | Reopen architectural clearance, document actual owners and phase/dependency directions, distinguish previous pointer/lifetime coverage from the new behavior review. Final claims must follow production wiring, not only helper tests. |

Existing equipment/pause inspection dispatch in startup also contains game
policy: grounding checks, next-weapon selection and transaction publication.
That code predates this HUD slice, but the new input owner must not preserve a
second conflicting arbitration path. The planned later directory-organization
slice is not permission to add new framework policy now. A narrowly named game
integration object may orchestrate the existing owners; it must delegate their
algorithms rather than become a replacement giant startup function.

## Evidence for the boundaries

Read recovered original exports, retaining raw decompiler material only in the
ignored evidence tree:

- `pass-233-gameplay-magic-ring/functions/00613300.c`: the player's magic action
  handles ring keepalive, cancellation, consumption and spell dispatch. It does
  not make those decisions in the platform pump.
- `player-resources/source/functions/00602b70.c`: player publication writes
  current/recoverable health and magic observations into the cockpit consumers.
  This is a game-domain publication boundary, not startup's interpretation of
  resource storage.
- `pass-230-gameplay-hud/functions/004993b0.c`: cockpit draw chooses eight native
  consumers under activation, visibility and pause conditions. Its visibility
  state is distinct from resource ownership and from the platform renderer.

These are inferred native owner boundaries, not recovered original C++ names.
The reconstruction may separate presentation arithmetic from integration more
cleanly, but the owner of each behavior must remain explicit.

For the C++ decomposition boundary, reread pinned Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/PlayerController.cpp`, from the existing
ignored reference snapshot. `ProcessPlayerInput` builds/dispatches the game input
component stack with a supplied pause observation. `ClientSetCinematicMode`
changes HUD visibility through the controller/HUD relationship, separate from
platform sampling. This supports game input/presentation ownership; it does not
supply replacement Bullet Witch input semantics, HUD timing or an Unreal
inheritance requirement. No source was copied into production by this review.

## Other inspected paths

- `PlayerResources` retains authoritative live lanes/magazines, const mission
  and clock borrows, checked frame windows and the restricted action capability.
  Copy/move is forbidden. HUD mappings must continue to use observations.
- `GameplayHud` owns gauge/reticle/ammunition visual state and emits bounded
  commands. `GameplayHudSurface` owns prepared resource leases and texture
  identities, consumes commands, and submits sprites. Neither receives resource
  mutation authority. The missing integration boundary is above these classes.
- `FrontEnd::update_power_up` is game-owned task orchestration. It delegates
  selection/purchase policy to `PowerUpFlow`, requests global fade and retires
  the task after completion. `PowerUpFlow` receives only
  `UpgradePurchaseControl`; the surface receives presentation values and cannot
  purchase or save. This placement is appropriate.
- `RootDriver` binds native power-up execution/wait; `StartupSession` schedules
  that game task. They do not invent mission success or apply purchases from the
  framework. The incomplete campaign save producer remains documented.
- The platform's additive sprite pipeline is a generic rendering capability;
  retaining it in the native backend is appropriate. Ring art selects a blend
  mode through generic commands without moving game progression into rendering.
- The reticle has implemented presentation/input-state observations; targets,
  hits and damage still need world producers. This review does not turn those
  default observations into gameplay parity claims.

## C++ consultation and acceptance of the repair

Queried both local corpora for explicit interfaces, cohesive data/behavior,
resource authority and bounded update cost. Retrieved and read Core I.1
(`#ri-explicit`) and C.1 (`#rc-org`), and performance COPY.5
(`guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md`) and MEM.6
(`guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md`). I.1 applies to explicit
input/output and admission contracts; C.1 to grouping actual related state
rather than sharing implicit relationships across the startup loop. COPY.5
informs exclusive resource identity; existing stable borrowers additionally
require deleted moves. MEM.6 preserves injected engine allocation at prepared
resource ownership boundaries; the repair must not allocate a new per-frame
policy object or hide duplicated live state behind shared pointers.

Corpus consultation is not automated architectural validation. The final review
re-read the corrected production caller, game owners and platform adapter, then
checked tests that exercise those same public integration paths. Required evidence:
ring consumption/cancellation, admission and resource snapshot publication;
pause/resume without HUD immediate-phase advancement; correct recharge receipt
and reticle mapping; presentation visibility and capture diagnostics; no new
writable clock/profile/resource access; and final repository verification.
Passing helper-only or source-arithmetic witnesses cannot close B1–B4.

## Production correction recheck

The production caller now constructs and delegates to distinct game owners:
`PlayerInputRouting`, `PlayerCockpit`, `MissionInteraction` and `MissionGameplay`.
Re-read their actual bodies and the shortened framework caller. B1/B2 are
repaired in code: the framework copies neutral input observations, supplies
presentation time, advances media between explicit game phases and rebases its
own pacing deadline from a transaction result. It no longer interprets spell
availability, consumes ring commands or assembles cockpit resource lanes.

B3 is repaired in code. The native adapter publishes separate held/pressed
shoulder positions and face positions, preserving simultaneous edges and
keyboard tap edges. The game routing owner chooses the combined-shoulder expiry,
face priority and consumption. Existing ordinary action bindings remain outside
this focused ring-policy repair.

The first repair's B5 was independently found before clearance. The revised
cockpit has separate update and drawing decisions: a consumed resume frame
redraws its retained presentation without advancing source phases; explicit
hidden activation invokes `hide()` to replace the retained sprite list. User
pause chooses the pause surface through `MissionGameplay`. The small
`GameplayHudOutput` interface exposes only synchronous `show`/`hide`, not resource
preparation, clock mutation or input policy.

The coordinator retains orchestration, not the component algorithms. Its frame
window opens once, actor resource transactions run inside it, recovery finishes,
and the cockpit receives a copied observation. Resources outlive Stage's action
capability; Stage and all other borrowers outlive the coordinator that retains
them. Power-up task ownership remains unchanged and game-owned.

The added in-process composition tests exercise actual routing, resource and
cockpit owners with a narrow output recorder: ring consumption preserves
movement/fire; pause freezes UI/random/resource updates; resume republishes
commands; hidden clears output; missing world prerequisites produce a typed
request with no magic debit. Native snapshot tests cover both shoulder states,
simultaneous face edges and press-only keyboard taps. Final repository and original-content gates passed as recorded below.
No visual acceptance or completed original-world startup is inferred.


## Independent semantic-freeze recheck

Re-read the corrected production framework, all four new game owners, native
button publication, surface hide operation and the reopened documentation.
B1–B6 are resolved in the inspected implementation. B6 now uses the typed
`observe_spell(SpellRequestStatus)` operation and maps only to static game text.
The public typed operation needs no whole-class friendship; the coordinator
cannot use this feedback seam to reach interaction internals or mutable
progression authority. No remaining actionable behavioral-ownership,
dependency-direction or borrowed-lifetime defect was identified in this scope. This is not a claim that all engine organization
is final; the broader directory pass remains separately planned.

The added mapping checks now exercise rifle/shotgun aimed reticles (original
sprite IDs 40/42), a real fire/reload transaction followed by resource finish and
cockpit publication, the recharge-driven cartridge ejection and no repeated
ejection on the next frame. The existing original-content corpus now invokes
production `MissionGameplay`, checks begin/advance/present ordering, then
pause/resume publication with real Stage/resource owners and a controlled output.
It adds no new standalone process scenario or fabricated gameplay producer.

Independently ran `ctest --test-dir build/host-debug -R '^runtime$'
--output-on-failure`: passed, 0.04 seconds total. Reviewed binary SHA-256:
`7b289050548e95929591a11a875ff78ecd514984104df39700db4b698416b4e7`.
This runtime target contains the new in-process routing/cockpit/native button
checks. It does not execute the original-content Stage composition check; that
result is recorded separately below.
`git diff --check` also passed at this recheck.


The integrator's final repaired `boundary-full-check.log` completes successfully:
build, all six CTest targets, fatal clang-tidy (202 translation commands, zero
failures), Python policy tests and repository content checks. This reviewer read
the completed log and independently reran the runtime target after the final
notice-interface change: passed in 0.03 seconds. Final runtime binary SHA-256:
`c754a27ad472f883dbae106dca1c57340e4deb692fe9f025da54ed234933248d`.
The final original-content corpus also passed in 186.618 seconds with texture
mips enabled. Read `boundary-actual-content/verification-summary.json` and
`stderr.txt` under the ignored gameplay-HUD evidence directory. Stderr explicitly
confirms production coordinator phase guards, pause retention and resume output,
followed by existing actor, four-weapon, melee and action regressions. Tracked CPU
memory returns to zero after teardown. Binary SHA-256:
`cb85fe03590cdeb67f438e0e5e02152932a1a3f6256d0a45fad4140eed0e0699`.
Independently matched the saved source hashes for the framework, all repaired
integration owners and native input backend to the current files.

This corpus uses actual original content and controlled rendering/media outputs;
it is not an original executable image comparison or visible game walkthrough.
No game was launched by this reviewer. No production code was edited, committed
or pushed by this review. Human visual acceptance and the already documented
world-dependent cast/target/damage/save-route boundaries remain separate.
