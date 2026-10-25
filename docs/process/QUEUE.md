# Remaining work

- The HUD/live-resource/ring and firing/support-hand work is accepted with
  its necessary existing-pause integration. The user confirmed the hand repair
  works and accepted the HUD, but explicitly did not accept the pause menu.
  Finish the original pause task next, with the user's missing-behavior feedback
  and a source-backed operation inventory. Keep post-result power-up work
  outside the accepted commit unless separately accepted. The architecture/file
  organization pass still follows this pre-gameplay work; full mission/casting
  and persistence dependencies remain open.
  Replace the current debug menu as the ordinary gameplay pause surface; retain
  debug views only through explicit build parameters, off by default.

- Separate offline asset-upgrade track: review Alicia's first body/face texture
  candidates from `research/reports/2026-10-25-asset-upgrade-pipeline.md`.
  Establish mesh/UV and moving-light comparison before accepting materials or
  batching. Raw inferred normal/roughness maps are uncalibrated candidates;
  preserve original meshes/rigs. Hunyuan runtime setup and later donor-mesh
  experiments remain future work; TRELLIS also needs authorized dependencies.
  Do not import experimental assets into the faithful baseline implicitly.

- The reopened gameplay integration responsibility repair is implemented:
  input arbitration, cockpit publication and mission sequencing are game-owned;
  platform adapters publish neutral buttons. The previous review missed this
  architecture defect. Replacement full gates, actual-content coordinator
  verification and fresh independent responsibility review pass. Resolve the live
  casting prerequisite boundary in `docs/architecture/player-gameplay.md` before
  claiming live casting readiness. Native Wall admission needs an actual wall/placement owner;
  Sacrifice needs an ally. Do not replace those with animation-review observations
  or grant extra magic. Full world save/checkpoint routing and mission-owned
  HUD activation remain explicit boundaries. Separate unaccepted working-tree
  power-up art, input/purchase policy and native prompt/layout verification are implemented;
  the repaired binary's final verification is recorded separately from the
  superseded architectural clearance.
  The user confirms the HUD generally works and accepts spell-ring navigation.
  Shotgun's duplicated attached-root transform is repaired. The all-four-weapon
  firing audit covers stationary/moving/aim transitions and original modifier
  ownership; the original player arm hook is registered but has no effect in
  this Steam image. The user authorized an established support-arm solver after
  the bounded numerical source comparison. Shared two-bone geometry, authored
  weapon-target integration and action-owned release are implemented and
  independently verified across all four weapons and exclusive-action
  transitions. This is an explicit improvement over the inactive PC solver;
  some moving class-2 frames retain a documented no-stretch reach limit.
  Final frozen-source gates and original-content verification pass; the user
  confirmed the repair works and accepted the HUD scope, not the pause menu. See
  `research/engine/subsystems/weapon-discharge-attachment.md`. Casting and the
  broader pause/power-up scope remain unaccepted. Verification: `research/engine/subsystems/gameplay-hud-verification.md`.

- Lighting redesign and its native surface-sampling repair were accepted by
  the user on October 25, 2026; commit is authorized. The final full gate and
  independent implementation/ownership review pass. Residual angle-dependent
  speckling and coarse shadows are accepted baseline limitations, plausibly
  original-era behavior but not established original-image correspondence.
  Final-normal normalization and pixel-footprint specular filtering remain
  explicit native adaptations. Stop this noise investigation; visual-quality
  improvements belong to the upgrade pass unless a concrete new defect appears.
  Latest reproduction references: October 25, 7:18:15 a.m. cave and 7:19:52 a.m.
  outdoor school outfit. Images remain outside Git. See
  `research/engine/subsystems/lighting-redesign-acceptance.md`.

- The progression/state slice is accepted and committed. It implements
  permanent snapshots/mutations, source costs/rewards, compatible Record save,
  bounded checkpoint-player decoding/merge, explicit concept entry and real
  weapon availability. It does not implement full gameplay save/world restore,
  earned-result production, story-grant bindings, HUD or power-up presentation.
  Current design: `docs/architecture/player-progression.md`.
- Step 2 timing, including the narrowed ownership contracts, is accepted after
  the user's interactive walkthrough. See
  `docs/architecture/world-timing.md` for verification and the remaining
  script/world integration and platform boundaries; those are not closed by
  this timing acceptance.
  The lighting slice is now accepted. Next is the agreed slice 3: real
  player/weapon resource ownership, controls, HUD, power-up transactions and
  supported pause operations. After the remaining agreed pre-gameplay work,
  complete a final architecture/file-organization slice before genuine One Shot
  Kill startup, authored entities and mission lifecycle. Discuss that layout with
  the user at that point: decompose the flat game directory, verify engine/game
  responsibility for scene code, and clarify lighting across layers. Directory
  placement must follow ownership and dependency boundaries; do not assume scene
  belongs under game or start moving files during the current slice. No final
  layout is selected yet. Preserve the other agreed work in its existing order. Preserve source-owned
  boundaries and inspect pinned Unreal implementations where original contracts
  remain opaque. Lighting's named event/weather/particle/settings producers stay
  with those owning slices; acceptance does not claim they already exist.
  Future gameplay acceptance includes the complete action: source input/admission
  and entity outcome, animation, relevant particles/effects, sound events with
  spatial placement/lifetime, HUD response, pause/timing and cleanup. Gunfire,
  impacts, reload, foley, spells, enemies and environment audio belong with their
  respective gameplay producers, reusing the existing backend. Silent/invisible
  partial actions do not count as completed gameplay; track real dependencies.
  This follows the organization pass and does not expand the present repair.



- Diagnose intermittent native fullscreen startup if it recurs. Two launches
  failed before a diagnostic-only rebuild subsequently entered fullscreen and
  movie playback. Failure logs now distinguish delegate rejection from a pending
  transition timeout and record activation/key state; launch behavior is unchanged.
  This is unresolved, not fixed by the successful retry. See the current evidence
  in `research/engine/subsystems/sprite-presentation-design.md`.
  The lighting-redesign walkthrough launch reproduced delegate rejection before
  a frame (`rejected=1 pending=0 entered=0 active=0 key=0`); lighting's native
  fixture checks pass, but this failed launch supplies no scene visual evidence.

- Video work is parked. The actual title-screen still is externally hosted,
  verified and embedded in the README; the GitHub description is updated.
  Keep capture files and metadata under ignored `research/evidence/public-demo/`.
  Do not resume video recording/editing as part of this pass. See
  `research/reports/2026-10-24-public-demo.md`.

- The accepted lighting baseline is the comparison point for the now-authorized
  source-faithful redesign. On October 24, 2026, the user accepted
  this slice for commit while noting it may be slightly too dark. Alicia's green
  hair on the default costume is specifically too dark; its color and readability
  are signature visual details. The completed audit supersedes the earlier
  deferral until gameplay: reconstruct the missing source contracts now without
  tuning authored values to fit appearance. Original initial texture contents and
  timing, exact outdoor appearance and the historical specific controller/visual
  comparisons below remain fidelity questions, not blockers to this accepted
  slice. Missing AREA activation/live environment publication, source pre-tone
  RGB composition and first-output luminance bounds are implemented and checked.
  See `research/engine/subsystems/character-material-brightness-audit.md` and
  `docs/architecture/lighting.md`.

- Preserve the sole authorized initial-placement shim for exact fresh
  `cm01_00`/`st04_c`, with no checkpoint restoration or hidden Stage fallback.
  Genuine mission startup remains incomplete and replaces that shim once its
  real dependencies are implemented. See the current table in
  `research/engine/subsystems/overfitting-and-shim-review.md`.

- Before the next implementation slice, reconcile the playable-level dependencies
  around the concrete next priority: real in-level pause/resume and a HUD bound
  to authoritative player/weapon state. With this slice accepted, this also
  includes the step-2 source-backed engine world tick and pause boundaries before
  more gameplay entities. The recovered variable interval and separate paused
  route are recorded in `docs/architecture/world-timing.md`; retain the remaining
  hierarchy/physics/script-side-effect limits rather than claiming a complete
  world scheduler. The current animation review UI does not
  establish gameplay input ownership, lifecycle or action feedback. Record each
  dependency's implemented behavior, missing behavior, prerequisites and
  observable acceptance, separating required gameplay from fidelity/upgrades.
  Include mission load/start/pause/death/retry/exit; animation-only versus
  world-affecting actions; enemies, hits, damage and required static/dynamic
  physics; and objectives, checkpoints, audio/effects and completion. Missing
  underlying state must be implemented explicitly, not represented by a mock
  HUD. This planning checkpoint follows the accepted animation slice; it does not
  expand the completed slice. Use the ownership inventory in
  `research/engine/subsystems/runtime-subsystem-inventory.md` alongside
  `IMPLEMENTATION.md`.

- Use the selected One Shot Kill target (`cm01_00`, extra ID 1, stage `st04_c`)
  for SRC-03. Follow the planned breakdown in `IMPLEMENTATION.md`: concept
  launch/mission ownership, required content loading, first static stage view,
  stage presentation, Alicia traversal, mission behavior and full outcome loop.
  Census actual stage/placement dependencies; do not infer an enemy-free level
  from the short Lua script. Preserve the recovered custom character collision boundary for traversal;
  dynamic-body provider and cooked-data treatment remain separate. Each slice includes its own
  failure/cancellation/release paths and architecture updates.
- Complete remaining front-end fidelity: wire persisted volume settings into the
  audio owner, add menu BGM/confirmation cues, reconstruct other options and
  submenu animations/localization. Keep these gaps separate from the delivered
  profile/menu/campaign-request slice. Resolve logo open-failure continuation,
  ordinary media-open rejection versus terminal decoder errors, and startup-wide
  initial fade state at their owning systems.
- Confirm the corrected right-thumbstick targeting path on the physical controller.
  Native GameController snapshot input through production Stage/Metal verifies
  second-click target exit and20°→44° camera transition independently from raised
  posture. Inactive Stop now preserves ordinary player state; unknown pose
  consumers remain visible but disabled. Keep the user's unidentified geometry
  selection separate from the demonstrated wrong-bank collapse. Scoped adversarial
  re-review covers these ownership/admission contracts, not all remaining shims.
  See `research/engine/subsystems/player-targeting.md` and
  `research/engine/subsystems/animation-review-lifecycle.md`.
- Retain the unresolved original-game comparisons from earlier animation
  review after the overall slice acceptance. The user accepted evasive timing,
  floor alignment and the shadow correction; forward/back evade facing had
  been rejected and has no separately recorded final retest. The bounded source audit and rendered four-direction witnesses
  match the recovered target/pose pipeline; obtain original-game visual comparison
  before changing directional policy again. The proven near-antipodal turning-side
  discrepancy is corrected; it does not remove the source-prescribed target turn.
  The inspected original footage supplies a raised-weapon backward flip but no
  matching neutral-run transition. The parent/model phase audit now proves preceding raw root consumption;
  the integration and native traversal recheck are complete. Review the integrated secondary
  bone/body-contact pass through in-session costumes. Expanded Alicia catalog,
  layer/event/facial/audio and accepted family presentation mechanisms are verified,
  with live action input, explicit external-event presentation review and
  transactional weapon-class selection connected. The reported movement rejection, missing live switch
  animation and aiming failures have source-backed corrections with the exact
  native sequences exercised. Grounded root motion now selects grounded contact
  explicitly, preventing the false airborne transition during stationary
  turn/fire. Overall slice acceptance does not imply each historical sequence
  was separately retested; evidence and remaining dispositions are in
  `research/engine/subsystems/alicia-animation-acceptance.md`. Combat outcomes
  remain separate unless explicitly added. Retain the earlier numerical sole-dip witness as a
  diagnostic limitation, not a claim that the user-reported floor bug persists. The subsequent planned slice
  is original mission behavior: start prerequisites, placement-driven
  enemies and AI/combat/damage, objective/trap/checkpoint callbacks, HUD/messages,
  gameplay audio and effects. Preserve the explicit boundary between bounded
  traversal and full mission start. Dynamic-body provider choice, impulses and
  destruction remain separate physics work; current placed props retain authored
  initial poses. Destruction-driven visibility, dynamic area changes and local-light
  lists need their owning consumers. Loading now presents the original portrait,
  sliding art, localized tip and default Xbox control icons during asset work.
  Full original loading-task exit and input-device-specific tip variants follow
  their future mission/input owners. Keep original Lua unchanged. The concept-menu completion gate
  alone is intentionally removed; campaign progression gates remain.
- Retain Alicia-shadow pixelation and slight distant mountain-shadow movement
  as original-game comparison points after the user-accepted receiver-footprint
  correction. Do not tune map resolution or stabilize projection without a
  demonstrated source/fidelity discrepancy. See
  `research/engine/subsystems/stage-shadow-presentation.md`.
- Hardware traversal now uses original LT evade, source directional root bases,
  radial input normalization and nonfiring walk/dash selection. Live attack/fire presentation and weapon-dependent locomotion scaling are
  connected; configurable trigger swapping, persisted look options and true
  ammunition/resource admission remain with their game/input owners. Keep these distinct from current
  traversal acceptance; see `research/engine/subsystems/player-evasive-leap.md`.
- Continue memory reconstruction when world/AI/worker consumers are introduced.
  The current startup-owner migration is complete; the recovered AI linear mode
  is distinct from the implemented root reclaiming heap. Establish reset and
  quiescence contracts before adding pools/arenas, page capabilities or
  asynchronous GPU retirement. Track fragmentation and retained backing using
  heap measurements; do not relocate live allocations or infer historical
  aggregate budgets. Choose application admission limits from representative
  workloads, with explicit external codec/native headroom. Full default-heap
  elimination still includes library callback/value/profile/path boundaries;
  see `research/engine/subsystems/memory-migration-completion.md`.
- Compare movie color conversion, pacing and input mapping against original-game
  observations when available. Current host bindings and initial black fade are
  explicit development choices; backend execution does not prove full fidelity.
- Extend root/mission native bindings as their owning game systems are implemented;
  missing bindings and unsupported resource/CALL handlers remain explicit failures.
- Extend the live game camera and prepared static spatial queries into
  dynamic-scene ownership; preserve the source clearance
  contract and measured query cost of the prepared broad phase.
  Close scene/visibility, lighting/pass, all-mode schedule and
  auxiliary-worker contracts at their owning boundaries as source reconstruction
  reaches them. Structural ownership is established; behavior remains explicit.
- Reconcile the eleven-scenario integration inventory with the five-scenario target
  through meaningful in-process contract extraction where appropriate. Preserve
  required content-policy and CLI coverage; add no integration scenarios meanwhile.
- Revisit ASan and ThreadSanitizer startup limitations when the host/toolchain
  changes. ASan hangs before main; TSan exits with signal 11 before main even
  for a minimal diagnostic program. Keep native/concurrency-contract/UBSan
  results separate from unavailable race-detector execution. Measure contention
  before claiming allocator throughput or latency improvements.
- Establish platform-specific verification when those backends are implemented;
  do not infer Xbox or Windows support from a passing Mac build.
- Compare remaining outfit/body intersections with original-game behavior;
  physical-controller review confirms the pause menu works.
  Actual-content native replacements across all six costumes preserve a frozen
  walking pose/camera and resume successfully, with balanced device teardown.
  Resume consumes its input frame and held evade requires release. Full original pause task animations,
  power-up, restart and mission-exit actions remain separate reconstruction work.
