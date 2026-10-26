# Mission simulation and retained presentation ownership

**Accepted after native walkthrough · October 26, 2026.** Scope is slice 1 of the
[accepted reorganization plan](../../../docs/architecture/reorganization-plan.md),
committed as `79d7a72`. Acceptance covers this ownership slice, not the remaining
reorganization slices or full gameplay.

## Existing responsibilities and selected decomposition

Before this slice, `StageInspection` owned static contact, Alicia/controller/camera,
targeting, audio observations, authored environment/light fields, draw preparation,
material channels and preview state. `PlayerActor` published pose/shape/visibility
through `WorldPresentation`, which also exposed scene replacement, lighting and
clear operations. Native `WorldRenderer` already retains the only draw registry.

The implementation separates three stable owners behind the existing caller-facing
composition boundary:

- `MissionSimulation`: static contact, actor/controller/camera, targeting, action
  resource capability, weapon settings and listener/pause observations.
- `StagePresentation`: authored environment/lighting/material interpretation and
  retained render-scene publication, borrowing completed actor/view observations.
- `PlayerAnimationReview`: existing preview state and diagnostic commands. Normal
  weapon and costume transactions remain production behavior, not debug behavior.

The portable `RenderScene` lifecycle owner borrows the existing backend. It does
not retain another draw or light registry. An actor publisher exposes only pose,
shape and visibility updates; scene membership, light/view/material publication
and retirement belong to the scene owner. Registration, replacement/removal and
retirement retain native validation and synchronous GPU completion. No new entity
registry, full-world copy, shader, physics behavior or asynchronous submission is
introduced.

## Preserved order and transactions

One admitted world frame remains authoritative. Existing actor parent/model
substeps run in their recovered order. Camera advances after actor motion;
environment sampling consumes the completed camera; its far-plane result is
applied before view/shadow/light publication. Material channels consume the same
world-total observation. Pause and resource transactions keep their accepted
clock admission and loading rebase behavior.

Costume/weapon preparation retains original resident assets until publication
succeeds. Render replacement precedes adoption into the stable actor and the
loading-owner commit. No temporary duplicate authoritative scene/light registries
are introduced. Scene removal/retirement precedes destruction of borrowed mission
assets, including construction-failure unwinding.

## Evidence and design guidance

Original [world timing](world-timing-source.md),
[stage presentation](stage-scene-presentation.md),
[lighting producers](lighting-scene-producer-contract.md) and
[draw replacement](model-draw-replacement.md) constrain behavior. The plan's
[pinned primary comparisons](../../../docs/architecture/reorganization-plan.md#reference-hierarchy-and-comparison-decisions)
support explicit retained membership and actor/component publishers. They do not
require copying Unreal's threading, whole-world snapshots or its class hierarchy.

Both required C++ corpora were queried for header design. Full Core I.1, C.9,
E.6 and I.11 and performance MEM.9/GPU.1 were read: explicit restricted authority,
encapsulated state, scoped retirement, documented borrows, prepared storage and
unchanged device residency. Implementation consultations reread I.1/C.9/E.6 and MEM.9; the game split also
uses R.21 and COPY.4 for stable noncopyable owner identity. Construction parses
customization once, then shares a setup-only const reference; no additional
per-frame storage or authority was introduced.

## Validation status

Scoped lifecycle/publication tests and native offscreen removal checks pass.
Independent review found no demonstrated authority, lifetime or ordering defect,
including the initial-versus-completed camera admission distinction.

Final `python3 scripts/check.py all` passes: native build, six CTest groups,
237 fatal clang-tidy command checks, pinned formatting, five repository policy
tests and content guard. The original-data `startup-corpus` traversal also passes:
scene admission/retirement, parent/model timing, appearance replacement and
rollback, all four weapon classes, pause/resume/reentry, preview phases,
melee/action exit and zero live migrated payload after teardown. That corpus
uses deterministic rendering/audio fixtures; it does not establish native
visual/audio equivalence. Native offscreen removal pixels were checked separately.

Logs remain under ignored `research/evidence/reconstruction/mission-presentation/`.
The user accepted the native walkthrough on October 26 ("Ok that works").
This closes the slice's native review gate without claiming separate confirmation
of every suggested input sequence. The user also reported a pink block over the
current costume-menu item and explicitly excluded it from this changeset. It is
recorded as a separate UI backlog item in
[QUEUE](../../../docs/process/QUEUE.md); its cause and original-game behavior
remain unverified. No implementation commit or push was performed as part of
recording this acceptance.
