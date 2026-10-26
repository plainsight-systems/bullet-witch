# Mission reentry from pause

The original mission owner distinguishes checkpoint return, stage restart and
retirement. `0x43c500` and `0x43c510` publish the first two requests;
`0x43eea0` consumes them after common teardown (`0x43eb50`, `0x43e9a0` and
the intervening cleanup states). Neither completes root `_WaitMission`.

Checkpoint return restores base/mission sections through `0x494020` and enters
mission initialization at state 600. Stage restart clears live mission state
through `0x494150`, serialized mission data through `0x493290`, and serialized
background/enemy/callback sections through `0x493240`, then restarts script
setup through `0x43c670`. Root Lua continues waiting for the same mission.
Retirement instead completes that wait, then root consumes StopBgm, concept
unload when applicable, and status 2 before returning to title.

The reconstruction publishes a typed `MissionTransition`. The session checks
its supported entry contract before composition destroys any live owner.
Composition then destroys mission borrowers and authorities in the established
order and acknowledges completion. A fresh composition owns new actor, clock,
resource, audio and script instances; no actor-only reset or snapshot is used.
Permanent progression is observed again at reentry. The committed live costume
is copied before mission resources retire and carried through the typed
transition request into the retained session entry.
The session retains the same root and mission identity across reentry.
The original costume selection `0x4c2cf0` publishes the selected model through
`0x441fb0`/`0x441ff0` and writes the live Record costume byte (`0x12f45d9`).
Record load/save (`0x4c2eb0`/`0x4c3050`) separately transfers that field to disk.
Neither restart branch reloads Record or reselects the costume. Return restores
checkpoint difficulty (`0x12f45d8`) but does not overwrite the costume byte.
Selected model resources use tag `0x6a`; explicit replacement invokes `0x44fd0`,
which the inspected restart/Return cleanup does not invoke.

The original pause offered no costume replacement. Our approved addition must
therefore bridge its committed appearance into that retained session choice.
Previously reentry replaced it from `Profile::costume()`, which still held the
front-end choice; this was a reconstruction integration mistake, not an
original reset requirement. `MissionTransitionRequest` now carries the
committed resource owner's costume before teardown. This does not alter Profile,
progression or save files. Retiring to title still ends that mission-only choice;
changing permanent front-end selection remains a separate operation.

Current admission is fresh One Shot Kill. MissionDriver reads checkpoint zero
and has not reached genuine script-owned world startup. With no captured
checkpoint, Return rebuilds that initial entry. Nonempty imported checkpoint
restoration is rejected before teardown: restoring health or position alone
would omit the original background, enemy and callback restoration performed
by `0x43c1e0`. A future world checkpoint owner must join this protocol before
nonzero checkpoints are admitted. Stage Start deliberately discards checkpoint
input for its fresh runtime entry; it never overwrites the saved profile.

Help reading updates live Record flags through `0x4de7b0`; Help close does not
save. Confirmed campaign retirement can run `0x4a6380`, whose sequence saves
System, checkpoint and Record, checking each operation. Original pause bypasses
that retirement save in concept and stage-select modes. Options' System save
does not imply saving Help Record. General mission checkpoint capture and
campaign retirement persistence remain separate owners.

Design follows these original boundaries directly. Core I.1 makes the request
and acknowledgement explicit; performance MEM.9 confines allocation to mission
teardown/setup rather than ordinary frame publication. These rules are queried
again during implementation. This document establishes the lifecycle contract;
it does not claim full checkpoint reconstruction or gameplay startup.

Verification distinguishes source and executable coverage: session tests check
request validation, retained root wait, refreshed progression and rejected
nonzero checkpoint restoration. The original-root corpus exercises actual
retirement and title return. A complete native Stage Start/Return teardown and
second scene entry were confirmed in the user's walkthrough, as were Help and
normal pause operations. That walkthrough exposed the costume handoff defect.
Session regressions now distinguish live and permanent choices across both
reentry requests and assert no Profile/progression or save-write changes. The
narrow change passes the complete build/test/format/fatal-lint/content gate
(`native-pause/costume-reentry-check-all.log` in reconstruction evidence). The
user subsequently confirmed the retained costume in the native walkthrough and
accepted the pause slice.
