# Gameplay resources and HUD: independent review

## Reopened responsibility finding

The subsequent user review found game input/HUD policy in application composition
and ring interpretation in the platform adapter. This review missed those
responsibility violations: its lifetime/const/transaction checks did not establish
correct architecture. Its original completion language below is historical and
must not be used to close the slice. See
[integration responsibility repair](gameplay-integration-boundaries.md) and the
fresh independent boundary review for the replacement disposition.

Reviewed 2026-10-25 against the uncommitted gameplay resource/HUD work based on
`3e985bc`, after the implementation agent declared its semantic freeze. This
review did not change production code, run the game, make commits or push.

The reviewed implementation has no remaining demonstrated defect from this
review's findings. That disposition covers the resource transactions, HUD and
ring ownership, pause/input routing and post-result power-up work described
below. It does not establish a playable mission, successful spell execution,
checkpoint restoration or a completed visible-window acceptance walkthrough.
Repository validation remains a separate gate.

## Method and scope

Read the repository instructions and architecture contracts, then followed the
actual callers and owners through `PlayerResources`, Alicia action admission,
`StageInspection`, framework composition, `GameplayHud`, `AmmunitionHud`,
`SpellSelectionFlow`, `PowerUpFlow`, `PowerUpSurface`, `FrontEnd` and `Profile`.
Reviewed the changed controller adapter and shared sprite submission as well.
Checked disputed behavior against recovered original routines and instruction
traces, rather than treating an implementation's own tests as its specification.

Consulted both C++ corpora: Core C.32 (`#rc-dtor-ptr`) and I.4 (`#ri-typed`),
and performance COPY.5 and MEM.9. The ownership review checked stable lifetime
of borrowed capabilities, one progression authority, read-only clock access,
bounded frame storage and the absence of per-frame resource preparation.
This is not a general review of all existing engine code or the accepted
lighting baseline.

## Findings and disposition

| Finding | Source or contract | Final disposition |
| --- | --- | --- |
| Both shoulders were treated as page navigation rather than the native expiry command. | Original input conversion and ring routing (`5fc3f0`, `613300`, `4d0d00`). | Resolved. The adapter detects one shoulder held while the other is pressed. The ring receives a distinct expiry command and page edges are suppressed for the chord. |
| Timeout/cancellation jumped directly to visual close, collapsing the input latch and visual lifecycle. | Ring update `4d0d70` finishes opening/page phases and reaches close through idle after keepalive is withdrawn. RS routing returns before supplying keepalive; it does not directly force the closing interpolators. | Resolved. Input admission, previous keepalive and visible phases are separate. RS withdrawal and combined-shoulder expiry use their respective paths. |
| Immediate reopen after expiry reset the page and visuals while the original still retained prior keepalive. | `4d0cb0` initializes only when current and prior keepalive are absent; `4d0d00` resets the page only on that fresh lifecycle. | Resolved. Immediate reopen preserves the existing phase/page; reopen after the keepalive gap initializes a fresh lifecycle. |
| HUD validation could mutate presentation before its nested ammunition owner rejected an out-of-range count. | Outer invalid-observation contract versus the ammunition maximum of 511. | Resolved. The shared maximum is checked in outer preflight; the regression retains the prior command publication and random state on rejection. |
| Power-up prompt fade had been conflated with selection behavior, and drawing selected only one of two prompt owners. | `472b80/472c70/472d10` control prompt opacity. Outer `49f000` and child `4d6820` draw their own prompt owners. | Resolved. Separate outer/child prompt values are published and drawn independently through transitions. Original labels and input glyph art are used. |
| Category motion omitted the per-row selection displacement. | `4c6570`, `4c64c0` and `4c6240` retain a kick and following displacement for each row, in addition to the selected row's periodic bob. | Resolved. Four bounded motion states use the shared response math and publish copied offsets. Values come from the executable's static parameters, not screenshot tuning. |

An initially suspected missing confirmation scale was withdrawn after checking
the enabling flag. The selector's scale impulse is conditional; the power-up
owners do not enable it. Keeping scale at one for this screen is supported by
that source path. Similarly, the prompt fade is not a delay task or a row
opacity channel. These distinctions were resolved before final review.

The final power-up flow also exposes the original setup, reveal, quote, dialog
retirement and exit phases instead of using one generic confirmation delay.
Purchases happen after dialog retirement and revalidate through the Profile
capability. FrontEnd retains ownership of the final global fade and completion
publication. Mac Return/Escape prompt glyphs are a documented host input
adaptation; their single-icon geometry follows the original glyph path.

## Ownership and algorithm checks

`PlayerResources` is a stable mission owner. Actor code receives only action
transactions; HUD receives snapshots. Its mission and world-clock borrows are
const and outlive it. The coordinator publishes the world frame, begins the
resource transaction window, admits actions and finishes recovery/cooldowns
once. No second elapsed-time source was added to resource admission.

Checked fire catch-up, partial-magazine results, empty-fire recharge dispatch,
reload cost and commit order against the original shooter/resource routines.
Four magazines and cooldowns remain resident across weapon changes. The removed
presentation cooldown does not remain as a competing firing authority. Paused
frames do not admit resource spends or advance ring/HUD phases; consumed pause
edges are not replayed as gameplay actions.

Ring selection produces a request, not a magic debit or a fabricated successful
cast. Power-up rendering cannot purchase, save, replace the Profile or advance
the world clock. Profile retains the authoritative Record bytes and stable
identity. Confirmed purchases dirty those bytes; a presentation snapshot is not
a second save model. Surface preparation owns leases and uploaded textures,
unwinds partial setup and releases them on retirement. Frame emission uses
prepared resources and bounded command arrays.

No additional concrete resource-ownership, save-authority, clock-authority or
lifetime defect was demonstrated in this bounded review.

## Verification evidence and its limits

Read the ignored original-execution comparison results rather than rerunning
production or launching a visible host:

- Ring lifecycle: 1,175 simulated frames across timeout/reopen and RS withdrawal
  cases; no reported mismatches and maximum visible-channel error
  `2.4155548095095014e-7`. The comparison excludes 582 unpublished or retired
  frames from numerical presentation comparison. These are frame observations,
  not 1,175 independent tests. The separate expiry/chord comparison also reports
  no mismatches.
- Ammunition comparison: seven observed states, no differences.
- Category motion comparison: no mismatches, maximum error
  `2.8637402342823748e-6` against the original selector update.
- The recorded repository build/test run in `gameplay-hud/full-check.log`
  completed with all six CTest targets passing.

The presentation author's report records actual-archive offscreen Metal probes
and texture/device-charge cleanup. This reviewer inspected the implementation
and its evidence boundaries, but did not independently run those probes or a
controller walkthrough.

At review time, the saved actual-content verification summary reported a
terminated run (`exit_code: -15`), not a pass. The previous targeted tidy summary
also retained a failing `startup_corpus` result while the integrator was rerunning
the final lint gate. Those records must not be described as successful final
validation; the integrating report must record any replacement completed runs.

## Open gameplay boundary

Spell selection and affordability are implemented, but real spell admission
still needs the corresponding target, entity and effect owners. In particular,
the initially affordable Ancient Wall and Sacrifice paths require genuine world
or ally context. A selected icon must not create a fake wall, target, debit,
animation completion or successful cast receipt merely to close this slice.
The next scope decision is whether to accept this resource/HUD boundary and
implement those producers with world gameplay, or extend the current work to
include the required original owners.

Damage/hit publication, complete world startup, mission result production and
the original world checkpoint/save-task behavior remain outside the reviewed
completion claim. Post-result power-up transactions do not prove a full
mission-to-save loop. The previously authorized initial placement shim is not
expanded by this work. Human visual/controller acceptance is still distinct
from the source comparisons and automated contracts.
