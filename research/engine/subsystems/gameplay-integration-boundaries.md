# Gameplay integration responsibility repair

The initial resource/HUD review checked narrow borrows, transactions and
lifetimes but missed misplaced responsibilities. Its completion claim did not
establish correct architectural boundaries. The launch framework interpreted
resources for HUD lanes, selected ring inputs, masked gameplay commands and
chose spell feedback; the macOS adapter encoded ring face priority and shoulder
expiry. Those game policies have been removed from both layers.

Original input/ring action `613300`, player publication `602b70` and cockpit
task `4993b0` are distinct native responsibilities. The repaired integration
retains `PlayerResources`, `SpellSelectionFlow`, `GameplayHud`, `PauseFlow` and
`StageInspection`, adding explicit game adapters rather than replacing them
with a monolithic loop owner:

- `PlayerInputRouting`: neutral button observations to ring commands; learned
  levels/affordability, command consumption and typed unavailable-owner result.
- `PlayerCockpit`: resource/targeting/ring observations to native HUD lanes,
  recharge receipts and source pause gating. Inspection activation is explicit;
  it does not claim `_DispCockpit` is wired through incomplete world startup.
- `MissionInteraction`: pause and equipment/review transactions, availability
  enforcement and review-catalog rebinding after successful publication.
- `MissionGameplay`: sequencing only—interaction, one frame publication,
  resource admission, routed actions, actor update, resource finish and cockpit.

The framework retains service construction, platform input sampling/adaptation,
presentation intervals, media advancement and phase delegation. A returned
resource-transaction fact lets it rebase the platform pacing deadline without
deciding what a costume or weapon operation means. Construction order keeps
resources alive through Stage retirement and the coordinator retires first.
No new Profile authority, mutable resource setters or alternate clock exists.
The existing FrontEnd/PowerUpFlow task/purchase boundary is already game-owned;
the framework's new PowerUpSurface construction supplies services/assets only.

For the bounded orchestration boundary, read Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`:
`Engine/Source/Runtime/Engine/Private/PlayerController.cpp`, `PlayerTick`,
`ProcessPlayerInput`, `PostProcessInput`; and `Private/HUD.cpp`, `PostRender`,
`DrawHUD`. Player input filtering and pause-aware command processing reside
with the player/controller layer; HUD owns visibility/drawing and observes its
player/world. The platform entry loop does not assemble health or ring policy.
We adopt that responsibility separation, not Unreal's object system or its HUD
clock arithmetic. Bullet Witch's recovered frame/pause semantics remain exact.

Both CPP corpora were consulted for header design: Core I.1 and C.9 make the
game adapters explicit and hide policy state; performance MEM.9 keeps bounded
frame state inline. Independent review must check responsibility placement in
addition to lifetime and const correctness. This focused repair is separate
from the later user-planned directory/layout discussion.

## Repair verification

Implementation consultations reread Core I.1/E.4 and performance MEM.9. The
fresh independent responsibility review closed the framework/platform leaks,
an intermediate retained-sprite issue on resume/hidden output, and an overly
broad text-borrow API. Feedback now carries a typed status to static game text;
no friendship grants the coordinator access to interaction internals.

The final full repository gate passes, including six CTest suites and fatal
clang-tidy. New in-process integration checks exercise actual game routing,
resources and cockpit together: face command consumption, typed missing spell
owner, pause/resume publication, hidden clearing, source aimed reticles and
real recharge receipt mapping without replay. The existing actual-content
corpus also runs the production mission coordinator against the loaded actor,
checking phase rejection and pause/resume output; it passed in 186.62 seconds
with zero live migrated CPU storage after teardown. See
[verification](gameplay-hud-verification.md) and
[the independent review](gameplay-hud-boundary-review.md).
These checks do not claim a visible walkthrough or complete world gameplay.
