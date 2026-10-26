# Current engineering decisions

- Frontend scope is the entire original reachable menu system. Lua orchestrates
  native tasks; source task implementations define artwork, localization,
  eligibility, transitions, input and audio. Generic child lists and invented
  labels are removed, with shared frontend Options and its actual child
  owners integrated. Expanded native build/CTest, all 233 fatal tidy command
  coverage, formatting/policy and purchased-content traversal pass. The user
  accepted the complete native frontend walkthrough on October 26, including
  reachable Power Up. Steam services and mission-result/world persistence
  remain explicit future boundaries.
  See `research/engine/subsystems/frontend-verification.md`.

- The user accepted the native gameplay pause menu after checking Help, Options,
  Stage Start, Return, Retire and live costume retention across reentry. The
  original menu, localized Help and settings use their owning input/audio/profile
  services. Restart carries the committed mission appearance without changing
  permanent selection, progression or save data. Full repository checks and
  independent source/ownership review pass; native walkthrough acceptance is
  distinct from controlled corpus checks. See
  `research/engine/subsystems/native-pause-menu.md`.
  Real nonzero world checkpoint restore, subtitle event producers and gameplay
  force-feedback remain explicit gameplay boundaries. The separate post-result
  Power Up implementation was included as WIP at the user’s request. Its
  frontend route is now accepted; post-result gameplay production remains WIP.

- Gameplay pause is separate from retained developer diagnostics. Normal builds
  disable developer views. The initial optional timing overlay, separate
  inspector and automation boundary are design only in
  `docs/architecture/developer-tools.md`; activation controls and detailed UI
  remain undecided. No speculative debug interface is included.

- The HUD/live-resource/ring and firing/support-hand repair were accepted and
  committed separately. Pause acceptance does not close live casting, genuine
  mission startup/results or world persistence. Finish remaining agreed
  pre-gameplay work before the architecture/file-organization pass.

- Offline asset-upgrade experiments are authorized in parallel with the faithful
  port, starting with Alicia's original outfit. The first repeatable extraction
  and texture/material pipeline preserves source bytes and resource provenance,
  compares bicubic, Real-ESRGAN and PBRify, and saves isolated candidate maps and
  review crops. The real body/face pilot and alpha-preservation control pass;
  visual acceptance and runtime integration remain separate. Keep original
  meshes, rigs and animation as masters. This does not reorder engine work or
  approve replacement assets. See
  `research/reports/2026-10-25-asset-upgrade-pipeline.md`.

- Before genuine mission/full-gameplay implementation, finish the current live
  resources/HUD/controls/pause/power-up slice and remaining agreed pre-gameplay
  work, then hold a final architecture/file-organization slice. The user will
  discuss that layout when reached: the flat game directory, engine/game scene
  ownership, and lighting across layers. Follow responsibility and dependency
  boundaries; no assumption that engine scene moves into game, no final layout
  or current-slice reorganization is authorized by this sequencing decision.
  Subsequent gameplay slices are accepted as complete connected behaviors,
  including source-driven sound, relevant effects/particles, animation, entity
  outcomes and HUD response with correct timing, pause and cleanup. Reuse audio
  services and trace event producers; silent/invisible actions and guessed cue
  timing do not establish completed gameplay. This does not widen the current
  HUD/animation repair scope.

- Slice 3 live resources/HUD responsibility placement was reopened by user review:
  game ring/cockpit policy leaked into framework and platform code. Prior
  lifetime/const checks missed architecture. The repair gives input arbitration,
  cockpit publication and mission sequencing explicit game owners. Fresh
  responsibility review, full repository gates and actual-content coordinator
  verification pass. See
  `research/engine/subsystems/gameplay-integration-boundaries.md`.
  The work remains based on accepted lighting
  commit `3e985bc`. Source-derived health/magic lanes and all four magazines
  remain mission-owned; HUD receives copied observations and action code a
  restricted transaction handle. Power-up belongs to the original post-result
  route, never a new pause purchase screen. The fresh magic ceiling is 20, not
  100; completed learning does not grant resources. Native HUD/ring assets and
  pause-only residency coexist within the unchanged texture budget. Live casting
  still requires a decision about genuine prerequisite owners; no synthetic
  effect/target success is permitted. See `docs/architecture/player-gameplay.md`
  for current scope and unclosed acceptance. Earlier architectural clearance
  is superseded by the fresh behavior-ownership review. The user confirms the
  HUD generally works and accepts spell-ring navigation. Shotgun's duplicate
  attached-root application is repaired; the all-four-weapon source/modifier
  audit distinguishes the inert original player arm hook from unrelated active
  NPC sway. It demonstrates no additional effective player-stage mismatch and
  does not establish original visual parity. The user rejected closing the
  stationary support-hand issue as a visual recheck. After bounded literal
  original producer/pose/hierarchy comparisons, the user explicitly authorized
  an established solver rather than more source-audit cycles. The implemented
  shared analytic two-bone solver uses the pinned Unreal reference and Alicia's
  authored weapon targets with action-owned activation. This is documented as an
  improvement over the PC's registered but empty arm solver, not original
  behavior parity. Independent actual-content review confirms stationary grip
  and exclusive-action release, with a documented no-stretch reach limit for
  some moving class-2 frames. Final frozen-source gates and original-content
  verification pass. The user subsequently verified the hand repair and accepted
  the HUD scope; pause completion and the casting prerequisite decision remain.
  See `research/engine/subsystems/gameplay-hud-verification.md` for the acceptance
  split and verification bounds.

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

- User acceptance of scoped work authorizes committing that accepted scope.
  It does not authorize pushing; keep unrelated work out of that commit.

- The accepted lighting redesign followed timing acceptance. The source
  audit found structural deficiencies, not a need for brightness tweaks. Design
  and implement distinct receiver policies/identity, complete material and
  surface dependencies, source pass/coverage order, view/shadow publication and
  output lifecycle. Carry every audit boundary through
  `research/engine/subsystems/lighting-redesign-acceptance.md`; known missing
  owners are implementation prerequisites, separate from original runtime
  evidence limits. Keep authored values and the recovered legacy light model.
  Current implementation includes exact material admission, strict/blended
  receivers with per-instance cache/reflection ownership, CP0 placed origins,
  overflow SH, source shadow selection/state, S/T and Main/Trans replay,
  output-history lifetime and final edge filtering. The full integrated gate,
  native contract checks and hash-bound original-root corpus pass. Visual
  acceptance is complete after the subsequent walkthrough and surface repair.
  An earlier failed fullscreen launch supplied no visual evidence. The user authorized
  completion of graphics mip construction and an independent ownership review.
  Immutable launch policy now covers world and front-end DDS resources, including
  four-bit/odd base conversion and full native chain accounting. Numerical
  original-DLL replay is exact; the full integrated gate and final actual-content
  corpus in both modes pass. Independent review found and resolved broad
  renderer authority, missing front-end mip coverage and witness truncation
  checks; its final disposition is recorded in lighting-independent-review.md.
  Renderer residency and presentation are separate borrower capabilities. Missing
  mission/event/storm/particle/effect producers remain named upstream work, not
  evidence-only blockers or silent substitutes.

- The progression/state slice is accepted and committed as `7fc3ca9`. Progression is
  decomposed from the original Record, power-up/result, mission
  application and save-task owners. Profile's retained Record is the sole
  permanent authority; typed snapshots and checked mutations avoid a competing
  mirror. MissionPlayerState owns effective inherited levels, selected weapon
  and separate script/context disabled-spell masks. Weapon preparation/cycling
  and paused selection consume this owner and commit only after resource/action
  success. Saved concept entry inherits Record and ignores campaign checkpoints.
  The explicit current-host default `--concept-profile development-completed`
  supplies minimum acquired weapon/spell levels without changing real saves or
  achievements; `--concept-profile saved` selects exact persistent inheritance.
  Completion does not imply maximum optional upgrades. Original costs, six
  purchasable spell rows, result table and campaign-only wallet cap are mapped.
  Purchases/persistence are callable; power-up UI, result/story-grant producers,
  combat resource ownership and full checkpoint restoration remain future work.
  Original Lua owns mask publication; do not preapply One Shot Kill's mask while
  genuine start gates remain blocked. Full `check.py all`, final formatting and
  the existing original-root corpus pass. The approximately 36-minute corpus
  includes the pre-existing traversal/animation acceptance route and controlled
  renderer; it is not an in-game performance measurement or native gameplay
  parity claim. Native profile storage round trip and zero live tracked CPU
  memory after teardown pass. See `docs/architecture/player-progression.md`.

- Public-demo catchup uses `./run --capture`: ordinary fullscreen startup/audio
  and 60 Hz host cadence, with developer status and review-only pause entries
  hidden. Build/runtime diagnostics go to ignored
  `research/evidence/capture/latest.log`. Interactive runs are untimed by default;
  verification retains its 300-second bound. Full `check.py all` passes; a native
  launch confirmed fullscreen movie output, a quiet terminal and exit 0 after
  window closure. This does not establish sustained FPS, a listening check or
  manual in-level overlay acceptance. The user authorized the README/capture
  changes for commit/push and parked video work. One actual alpha macOS title
  still is now hosted on R2 and embedded by URL in the README; public bytes and
  image content type match the local capture. The GitHub description is updated.
  Keep image bytes outside Git and preserve the existing license scope and
  evidence rule. See
  `research/reports/2026-10-24-public-demo.md` for primary release sources and
  publication handoff.

- On October 24, 2026, the user accepted the current Alicia traversal, animation
  and lighting slice for commit. Lighting may still look slightly too dark;
  retain that observation for future source/footage comparison without tuning
  this accepted baseline. This closes the slice review gate, not full-game
  parity or every historical control/visual comparison. Remaining gameplay and
  ownership work is recorded in QUEUE and
  `research/engine/subsystems/runtime-subsystem-inventory.md`.

- Faithful-port performance acceptance targets stable 60 fps (about 16.67 ms),
  adopted from the official Steam PC baseline. Record output resolution, target
  Mac/hardware, scene, build and timing scope for acceptance; none is invented
  or claimed achieved by the incomplete scene. Published 720p30 minimum hardware
  remains a reference; recommended hardware has no documented FPS/resolution.
  See `research/engine/subsystems/steam-performance-baseline.md`.

- The sole user-authorized temporary shim is fresh One Shot Kill initial
  placement, isolated in `initial_placement_shim` with exact mission/stage and
  no-checkpoint guards. No reusable controller/Stage fallback owns these
  coordinates. Remove it when original mission-start gates and Lua player
  commands run against the real world; this does not claim startup completion.
  Earlier adversarial ownership and lighting findings have scoped re-review
  clearance. Subsequent physical review reopened melee during weapon lowering
  and costume brightness. The source-owned complete Shooter state matrix and
  synchronous entry correction now pass scoped re-review and the rebuilt PW5
  four-class native interruption witness. The combined gate, final original-root
  corpus and rebuilt native replay pass.
  The brightness defect was a missing live environment producer: stage activation
  enables AREA, whose camera-selected scalar channels replace raw CSTM defaults.
  The cave character-field target is 0.005 rather than one; ambient, directional,
  fog, sky, shadow and output settings update with it. Scene-ready startup requests
  one immediate publication, then world updates perform the original transition.
  The engine owns prepared environment and lighting state; the game maps original
  channel meanings. The source pre-tone RGB operation is restored before metering,
  bloom and final output. No costume scalar, art retuning or replacement metering
  algorithm is used. Settled native PW5/PW0 and region-transition captures improve
  substantially. Native first-presentation output now bounds its actual GPU
  measurement as the original does even with empty history; final PW5/PW0
  first-visible captures show no initial flash. No fake seed or warm-up is used.
  Original initial texture contents/timing remain unproven; the user has accepted
  this slice with the slight-darkness observation recorded above.
  See `research/engine/subsystems/character-material-brightness-audit.md`,
  `research/engine/subsystems/targeting-melee-arbitration.md` and
  `research/engine/subsystems/mission-start-admission.md`.

- Scene lighting is an engine-owned subsystem. `scene::SceneLighting` owns
  decoded fields with generic scene-local identities, independent intensities,
  authored emitters, directional/ambient parameters and prepared view admission.
  Game code supplies definitions and complete model observations; controllers
  do not sample or rank lights. Shared directional values are separate from
  owned receiver packets. Static view lights and spatial receiver active lists
  preserve their distinct source candidate sets and ordering. The owner is
  serialized, retains prepared domain storage, and publishes fixed values to
  rendering; it is not a completed entity registry or asynchronous scene.
  `scene::Environment` separately owns region/curve bindings and retained
  transitions. Game compatibility supplies the channel schema and source policy;
  the existing world update drives it without another clock. Scene-ready startup
  sets the source one-shot snap flag; later region changes blend normally.
  Final `python3 scripts/check.py all` passes: build, all six CTest targets,
  fatal clang-tidy, policy tests and repository content checks. See
  `docs/architecture/lighting.md` and
  `research/engine/subsystems/scene-lighting-pass-contract.md` for fidelity limits.

- The user accepted step 2 timing and its ownership corrections after an
  interactive walkthrough, reporting that timing felt right. No individual control
  sequence or broader lighting/gameplay acceptance is inferred. Engine FrameClock
  owns monotonic presentation
  sampling, the source 100 ms cap and pacing/rebase; WorldClock
  publishes the original scaled float interval and admitted simulation time.
  One MissionControl owns original user/script/pad pause state and neutral-input
  admission. Stage borrows these owners and has no local material master clock
  or competing pause flag. Root/control work continues while normal scene work
  pauses; the mission coroutine has its separate user-pause gate. Existing
  actor-local subdivision and media/device clocks retain their distinct roles.
  Shared input-repeat and native exposure consume explicit presentation time;
  their former independent target wall samples are removed. Full build, six CTest
  targets, fatal lint and content policy pass, as do the existing 36-minute
  original-content corpus, Stage/Metal timing witness, HDR presentation timing
  witness and native windowed four-movie startup. The known fullscreen activation
  failure remains separate. The review correction narrows Stage to const control
  and PauseFlow to user-pause commands; authoritative owners cannot copy/move.
  Full gate, rebuilt native timing/startup and the 36-minute 33-second original
  corpus pass after these API changes. Loading callback state outlives its holders
  by declaration order; compile-time checks enforce restricted mutation and
  stable owner identity.
  Complete Lua script-pause side effects, paused hierarchy, transition exceptions
  and physics/entity phases remain explicit future consumers. No fixed-world
  timestep was inferred from 60 Hz presentation. See
  `docs/architecture/world-timing.md`. Next, design the lighting subsystem
  reconstruction around the audit's structural source-fidelity deficiencies;
  HUD/resources remain queued afterward.

- Use subagents by default for task execution, keeping
  the main conversation available for planning and parallel exploration. Assign
  clear ownership and integrate/check their results; independent agent review
  remains on request.

- Preserve header-first contracts and algorithm design. When implementing bodies
  or filling out headers, put brief intent and invariant comments above the
  relevant code or declarations. Keep full design in opening contracts; local
  comments identify meaningful pieces without narrating obvious code. See
  `docs/process/workflow.md`, Comments beside implementation.

- README order: project purpose, how to get it locally, how to run it, then
  background, status, development details and other reference material. Keep the
  top-level `./run` command visible in the running section.

- Current CPU animation/secondary constraints and GPU skinning/shading follow
  the traced Steam baseline, not a recommended modern engine partition. Keep
  broad target-aware CPU/GPU repartition for the improvement pass after the
  working port; measured blockers or demonstrated defects can justify focused
  earlier changes with documented evidence and native verification. Unresolved
  paths are not covered by a whole-engine fidelity claim. See the work-placement
  policy in `docs/architecture/world-renderer-reconstruction.md`.
- Source reconstruction is the implementation direction: preserve behavior,
  establish a direct port, then improve the engine and graphics.
- Slice 5 actor presentation now has mission-owned selected-costume and weapon
  resources, immutable prepared clips, reusable skeletal poses and native skin
  palette updates. The original model basis, height offset, player mesh mask,
  attachment bone and weapon stand motion are preserved. The bounded traversal owner now connects root-driven movement, custom static
  contact, physical jump/landing and the live camera. Native actual-content
  numerical verification covers all three authored placements, with zero domain
  allocation/release during updates. User review now accepts the corrected evasive timing and floor alignment.
  Forward/back movement into evade still has rejected apparent facing after the
  first reference reset; the bounded source audit supports the current pipeline,
  so exact neutral-run original-game visual comparison remains needed. A
  machine-code witness identified and corrected the near-180-degree turn-side
  fallback, preserving the source target; inspected PC footage only establishes
  a raised-weapon backward flip, not the rejected neutral-run transition. See
  `research/engine/subsystems/player-evade-visual-reference.md`. The recovered outfit
  secondary pass is now integrated and native-verified across all six costumes;
  user review of garment/body intersections remains open.
  The user accepts the later floor/character shadow correction; remaining
  pixelation and distant-map motion are original-game comparison points. The native menu route and keyboard movement,
  jump/landing, camera look and pause/resume were observed in the running app. See `research/engine/subsystems/player-traversal-acceptance.md`. Animation root displacement is consumed once and locked out of the
  render pose. Source re-evaluation found hardware LT selects a directional
  root-driven evasive leap; the earlier generic scripted jump mapped to A was
  incorrect. All XYZ root displacement uses the source Waist_direction basis
  (sampled from the selected prepared clip each update), landing plays at 2x with cancellation frame 25, and live model height
  uses negative effective radius. Ordinary nonfiring input now preserves source
  radial scaling/deadzone and 0.9 walk/dash rate selection. See
  `research/engine/subsystems/player-evasive-leap.md`. Preserve source movement above
  shared collision queries; dynamic-body backend selection remains separate.
  See `docs/architecture/actors-and-animation.md` and
  `research/engine/subsystems/player-collision-boundary.md`.
- Current user review reopened Stop preview, persistent aiming and an unnamed
  geometry-breaking selection. The native menu Stop path resumes movement; no
  separate reset defect has been reproduced. User retest rejected the earlier expiry-only aim fix. A separate source
  targeting owner and original camera contexts now implement immediate mode
  exit independently of raised posture. Scoped adversarial re-review clears this
  owner, inactive Stop and source-bound classification; physical confirmation
  remains pending. Unknown and incompatible ordinary/additive representations
  are disabled/rejected before changing the active preview. Complete model
  substeps and balanced host subdivision correct independently demonstrated
  stale-event and tiny-remainder defects. Native mixed-rate aim/Stop replay and
  original-root assertions pass; user confirmation remains pending. See
  `research/engine/subsystems/animation-review-lifecycle.md` and
  `research/engine/subsystems/animation-review-geometry.md`.
- Expanded animation now connects source-backed player action inputs, explicit
  in-scene presentation review and transactional weapon-class selection. The
  integrated build, original-root corpus, native menu pages and complete native
  review sequence pass. Raised turning preserves raw source vectors through
  branch selection and normalizes the final facing; both directions pass for
  all four weapons. User review exposed legal-stick rejection, missing live switch motion and
  visibly incorrect aiming. Source-backed corrections now preserve the capped
  input magnitude, request the original switch while retaining body clocks,
  separate raw facing from displacement references, and apply both weapon
  attachment/render conversions. Grounded contact is explicitly distinct from
  airborne ascent: tiny positive root Y no longer causes stationary turn/fire
  to lose support. The original failing native sequence now passes across all
  four weapons, with steady and held-fire pitch side views inspected. The full
  automated gate passes; user acceptance of this corrected build is pending. Combat outcomes remain outside this slice. See
  `docs/animation-review.md` for controls and synthetic-observation boundaries.
- Expanded Alicia animation now prepares all 161 body motions and 164 parameters
  with alias/resource identity preserved. Stable body/upper/additive/weapon
  rings drive masks, blends, facial/visibility events and source action-family
  policies. The parent consumes the preceding raw root interval through the
  current selected reference; the model then advances once and publishes pose,
  events and the next interval. Raised aim uses the admitted five-bone chain
  and original scaled-marker quaternion arithmetic. Native forward/up/side
  aim and three-checkpoint scripted/reaction/pause captures pass. Motion-driven
  scene sound/voice and pause checks pass the actual-root corpus. A production
  motion-driven CoreAudio run completes without reported device errors and
  with balanced teardown; subjective audibility remains for user listening.
  Combat, world spell effects and dynamic death handoff are separate capabilities.
  Live weapon-class presentation review uses isolated resource/actor candidates
  and atomic renderer publication, preserving the frozen body and resetting the
  new weapon action owners. All four models and 26 weapon motions are admitted. See
  `research/engine/subsystems/alicia-animation-coverage.md`.

- First playable mission target selected October 23, 2026: One Shot Kill
  (`cm01_00`, extra mission ID 1), using `st04_c` stage resources. The planned
  breakdown in `IMPLEMENTATION.md` separates mission/content loading, a first
  static stage view, stage presentation, player traversal and mission behavior.
  Slices 1–5 are authorized. Original concept launch follows native internal
  status 5 to API 6 and selected-package aliasing to cm01_00. Its original load
  now returns naturally after typed stage/collision/material-animation owners,
  text/cover/audio registration and character/weapon dependency construction.
  The mission waits at `awaiting_world_setup`; selected-costume actor
  presentation prepares clips and a bone-attached weapon. Full mission start remains incomplete. The normal concept path enters
  a separate bounded traversal lifetime with authored placements and pause,
  without completing unsupported mission callbacks. Original-root integration verifies 54 dependency model variants,
  11 shared character support sets and 178 NPC palette bindings, with balanced
  teardown. The static view borrows those load-owned GPU resources and selects
  all ten ordinary stage models. Slice 4 now selects 4,100 ordinary draws,
  674 placed-prop draws from 276 placements, and one sky draw, with no omitted
  material draws in the actual corpus. Placed props retain their authored initial poses; dynamic-body simulation is not implemented.
  The live camera decodes the authored behind-player
  preset, follows the corrected player pose and uses the original static
  triangle-clearance sequence with a prepared broad phase, then supplies the original
  fixed-aspect reverse-depth projection. Generic stage customization and scene collision remain engine
  owners; player camera policy lives under game/camera. Full mission start has not executed; bounded actor simulation consumes the
  authored placement independently while the live camera follows its resolved pose. Slices 1–3 are implemented, verified and accepted for
  commit on October 23, 2026. Slice 4 is implemented, verified and accepted for
  commit on the same date. Slice 5 runs through the normal menu route. Expanded
  animation requires a fully reachable, checked review path; user acceptance
  remains separate. Timing, height and shadow correction are accepted. Neutral
  backward-evade comparison and mesh-aware outfit improvements retain their
  documented dispositions rather than blocking all animation inspection.
  Shadow projection matrix and edge-filter reconstruction retain source/device
  verification from slice 4. User review reopened visual shadow stability:
  extra source-excluded caster chunks and demonstrated receiver self-shadow
  artifacts are corrected. The user accepts the improvement; pixelation and
  distant mountain-shadow movement still lack original-game visual comparison. See
  `research/engine/subsystems/stage-shadow-presentation.md`. The established
  matrix/filter work is distinct from that visible residual:
  stage activation supplies the recovered perspective-warp controls, six original
  machine-code witnesses verify the camera-dependent matrix, and caster/receiver
  consumers share its homogeneous rows. The unsupported original address state
  uses a deliberate Unreal-informed cleared filter border, with derived guard,
  matching inset/scissor and native edge/corner checks. No global-AABB fallback,
  texel stabilization or cascade optimization remains.
  Corrected native captures show cave, exterior ground, buildings, mountains and
  sky. The host forwards authored output settings; shader selection includes
  the original runtime SH feature, with white sky coefficients and authored
  stage ambient RGB. No exposure adjustment fixed the black sky or clipped
  ground. An explicit `--inspect-camera` override supports exterior review while
  normal launch retains the source camera. Slice 4 adds source group masks,
  body-to-joint prop poses, fog, single-level texture sampling and retained shadow
  resources. Two unique authored material curves drive six UV updates through
  prepared slot bindings and a native destination index. The full required gate,
  resource/startup corpora and native initial/exterior/movement observations pass.
  Optional generated mips are not requested by the original default/installed
  setting. Current native evidence is recorded in stage-view-completion.md;
  earlier rejected captures are historical. The user explicitly removed the concept-menu completion gate:
  fresh profiles can select supplied extra missions, while campaign Stage Select
  and higher difficulties retain their progression gates. This intentional
  departure changes menu access, not content availability or original Lua.
- When bounded source tracing cannot establish an original algorithm, the user
  authorized using pinned Unreal source to guide that unresolved part and
  continuing implementation. Preserve verified facts and label inferred choices
  explicitly. The cleared shadow-filter border is such a choice. The provisional
  shadow bounds fit was replaced when the missing source initializer was recovered.
- The initial port preserves recovered behavior. Ambiguous implementation
  choices must use established design, data layout and cost reasoning, not a
  naive loop followed by patches. Consider batching/data layout and CPU SIMD
  (ARM NEON / Intel SSE or AVX) where CPU work can benefit; this is not GPU
  offload. Remove redundant work and measure representative native paths to
  choose target-specific implementations. The current scalar material sampler
  is accepted for this pass, not prescribed for future animation workloads.
- Memory direction approved October 23, 2026: follow recovered Bullet Witch
  allocation, ownership and retirement behavior first, with native target
  backing. Unreal and modern allocators inform the implementation; they do not
  replace the recovered contracts by default. Preserve verified coalescing;
  investigate fragmentation and reclamation as callers and teardown paths are
  reconstructed, then improve from evidence. Do not assume movable live
  allocations or invent historical budgets. See
  `research/engine/subsystems/memory-management-research.md`.
- Memory allocation must support target/silicon-aware implementations behind
  common headers. Shared code owns lifetime/admission/accounting and reusable
  algorithms; selected native backends own CPU backing, device placement,
  visibility and completion. Target-specialized allocation strategies may
  replace hot paths while preserving the common contract. See the explicit
  boundaries in `docs/architecture/memory-and-residency.md`.
- The front-end now loads/persists profiles, presents interactive menus and
  original costume preview sprites, and captures an owned campaign gameplay
  request through original Lua. Steam operation 2300 makes all five supplied
  costumes available; achievement 13 still gates campaign Stage Select, and 9/10
  gate Chaos/Hell. Concept Mission access is enabled independently by the
  approved reconstruction policy. The app currently supplies fresh achievement
  state. Native accessor0x49a560 resolves internal extra result5 to API6; selected
  concept aliasing is mapped in mission-launch.md. Current startup memory
  ownership is implemented; mission/world consumers extend that foundation next.
- Platform design follows the agreed DoLittle split: single portable subsystem
  headers, shared behavior/algorithms where target-specific optimization adds no
  value, and native implementations for device/platform work. Startup
  uses Metal for Mac presentation; Xbox implementations remain future work.
- The GitHub repository is `plainsight-systems/bullet-witch`. It uses the
  DoLittle split: Apache 2.0 for project-authored code/configuration and CC BY 4.0
  for project-authored writing/diagrams/original visuals. NOTICE credits
  Plainsight Systems LLC and records third-party terms. Original game content
  remains excluded; research tooling stays tracked under the existing policy.
- Original-engine mapping lives in `research/engine/ARCHITECTURE.md`.
  Reconstruction design lives in `docs/architecture/` and is the working structural baseline.
  It describes the finished engine: application/engine/session/mission/world
  ownership, world simulation, rendering, execution/memory and source boundaries.
  Runtime scenarios support that structure; delivery/migration order belongs in
  `IMPLEMENTATION.md`. Proposed authored game code belongs in `src/game/`; the
  ignored root `game/` remains reserved for local data.
- SRC-01 implements an owned C++20 RAT/CPR reader, catalog resolution and a
  read-only JSON inspector. The resource service now publishes supported USER
  payloads, retains leases and releases tag interests; DPK v1 access is implemented.
  No rendering or other record-handler execution is claimed.
- Format readers now own pure decoding in `formats/`; `ResourceCatalog` owns its
  decoded `ResourceCatalogData` and lookup index. `content/access` composes reads/location
  through the injected `platform::Files` capability and `HostFiles` adapter.
  The umbrella header gathers descriptive resource types; file opens use
  `open_archive` / `open_catalog`.
- Startup runtime now has task lifecycle/active/late passes, an owned Lua 5.0.2
  context, bounded 32-bit chunk conversion, wait/immediate bridges and a root
  driver. Engine mechanisms live in `bw_runtime`; game composition in `bw_game`.
  Original root routing reaches title/menu with deterministic test services.
  `CompanyLogoFlow` and `StartupCheckFlow` now implement bounded game update
  policies with in-process verification. The concrete `StartupSession` owns root/screen tasks and preserves
  idle/Wait/destruction ordering (screen priority 0x2000, root 0x2001). The reusable
  engine video Player owns playback control, consumed-media timing and completion.
  Its decoder owns independent demux/audio/video workers and bounded queues;
  the Mac MediaAudio service feeds AudioQueue independently of rendering. Both
  company movies completed the native run with exact PCM consumption, zero
  underruns and terminal-frame presentation; the final XSEED logo was also
  observed on screen. Valid signed source timestamp corrections are preserved
  separately from monotonic device time. The game adapter owns recovered fade
  arithmetic. Mac native code
  owns Metal/AudioQueue/window/input. The two company movies are marvelousLogo
  and xeedLogo. Advertise and title reuse that transport; FrontEnd now supplies
  character-load and menu policies through the same task lifecycle. Logo open-failure
  continuation is an explicit unresolved failure; see `docs/startup-runtime.md`.
- The engineering workflow requires both C++ MCP corpora in design and
  implementation, opening file contracts, pinned Google formatting, fatal
  clang-tidy and strict builds. The check entry point is `scripts/check.py`.
- Local game inputs are read-only. Generated research and logs stay organized
  under ignored `research/`; authored tools and reviewed summaries may be tracked.
- Native macOS and UBSan verification are available. ASan startup hangs before
  main on the current host; no passing ASan claim is made. ThreadSanitizer also
  fails before main with signal 11 in a minimal diagnostic; no TSan pass or
  race-freedom claim is made. Other target builds
  remain unverified. World rendering now supports bounded player traversal and static scene presentation;
  combat, dynamic world simulation and the complete mission loop remain unimplemented.
- Existing integration coverage exceeds the imported five-scenario target;
  the explicit adoption exception now enumerates eleven scenarios, including
  the provisioned original-root integration required for the startup slice.

- Engine API and file names describe responsibilities. Original opaque terms
  remain only where compatibility/provenance needs them. The naming map in
  `docs/architecture/file-mapping.md` connects resource archives, catalogs and
  named-file packages to their original formats. `TaskLifecycleScheduler` is
  explicitly single-threaded and not thread-safe; it is not a worker executor.

- Native shaders are standalone backend source assets, compiled by the build.
  The Mac movie shader lives in `src/platform/macos/shaders/movie.metal`; the host
  loads its compiled library beside the executable. Common headers carry no
  native graphics types. GPU optimization follows measured timing/bandwidth
  while preserving baseline behavior.
- Movie decode uses a checksum-pinned minimal shared FFmpeg 7.1.1 build with GPL,
  nonfree and version-3-only components disabled. Owned BGRA/PCM samples cross
  the portable boundary. No system FFmpeg selection or game transcoding occurs.

- The current movie decoder handles pre-recorded assets, not in-engine cinematic
  camera/scene/animation behavior. Revisit native Mac decoding as the port grows;
  the common owned-sample header permits another backend without changing game
  policy. Shared decoder source is not evidence of Xbox or Windows deployment.

- Memory migration now covers current native startup catalogs/archives/services,
  root Lua, task storage, owned media/image/font buffers, presentation state and
  Metal texture accounting. Resource/runtime/media domains are explicitly
  selected at the target entry; default aggregate limits remain uncapped pending
  measured application policy. The root's recovered mode is reclaiming; its
  bounded first-fit heap splits/coalesces and never silently grows. Context and
  sample owners survive yields/decoder teardown. Texture retirement follows the
  existing GPU completion wait. Legacy tools and external library/native overhead
  remain explicit boundaries. World pools, AI linear heaps, phase arenas and
  asynchronous device retirement await concrete consumers. See
  `research/engine/subsystems/memory-migration-completion.md` for exact coverage
  and verification; `docs/architecture/memory-and-residency.md` remains the
  broader subsystem contract.

- CPU domain locks protect reservation/commit/rollback counters, never native
  allocation/free or zeroing. Pending reservations count against limits and
  appear separately in snapshots; releasing storage keeps its charge until
  native free completes. The root Lua coalescer requires exclusive owner access
  without an internal mutex; synchronized thread handoff is allowed. See
  `research/engine/subsystems/memory-concurrency-refinement.md`.

- Standard engine subsystems follow established implementations first. Inspect
  concrete mature source, pin references, record adopted algorithms and justified
  departures under organized research, then implement and verify the relevant
  native behavior. This is mandatory workflow, alongside both C++ MCP corpora;
  do not reinvent baseline mechanisms or treat mock results as device acceptance.

- The media redesign is approved. Use Unreal's Media Framework as the engine
  architecture guide: shared playback control and independent sample/audio/video
  responsibilities, with native implementations under `src/platform/`. Original Lua
  and reconstructed game screen code retain playlist order, skip/fade policy and
  transitions for the faithful port. Movie playback has no logo-specific rules.

- Source organization follows the inspected Doom 3 game/engine pattern: one
  `src/` implementation root, common contracts under `include/bw/`, target-owned
  entry points under `src/platform/<target>/`, and shared application composition
  under `src/framework/`. Framework composes game and engine; engine mechanisms
  cannot depend on game code. The Mac entry selects native services and the
  explicit FFmpeg factory. Shared video, FFmpeg, native audio and native
  presentation have separate build targets. Future target entries reuse the
  shared framework; no additional target implementation is claimed.

- The opening cinematic and title flow preserve recovered timing/input/status
  contracts. Title uses original PNG images, FONT glyphs and packed atlas
  channels through shared bounded decoders and renderer-owned Metal textures.
  The game adapter owns title/font/selected-loading resource interests; encoded
  publication is distinct from decoded/device readiness. Native startup defaults
  to fullscreen after AppKit transition completion; `--windowed` and size options
  remain available. Version glyph flags cross an original BGRA convention and
  require explicit red/blue mapping into canonical RGBA sampling.

- Corrected native fullscreen acceptance now covers all four startup movies and
  title outcome 2, with full PCM consumption, no underruns and terminal frames.
  The original START/copyright overlays and corrected Version1.0.5 glyphs were
  visually inspected. Late video-frame drops remain observable; original-game
  audiovisual parity is not claimed. Strict checks, UBSan and real-root/asset
  integration pass. Profile/menu integration is now implemented; confirmation
  sound remains a gap.

- Profile persistence now owns original System/Record bytes and a validated opaque
  checkpoint. Missing files use recovered defaults; failed loads retain prior
  state, and explicit continue-without-saving disables writes. The portable
  SaveStorage contract has a POSIX atomic-file implementation and Mac application
  support directory selection. This is per-file replacement, not a multi-file
  transaction or checkpoint writer. Steam content-refresh flags are separate
  from achievement-based menu gates; see profile-persistence.md.

- Menu policy and rendering remain separate: MenuFlow publishes bounded rows;
  StartupMenuSurface uses existing SpriteOutput and original PNG/FONT data.
  Costume previews are authored 2D images, not a world-rendering dependency.
  FrontEnd gates readiness, fades and persistence; StartupSession captures
  StartMission as owned launch data and leaves WaitMission pending. The host
  consumes One Shot Kill into owned mission/load state and bounded player
  traversal; other routes still stop at the request boundary. Current options
  cover persisted volumes and costume; audio gain, BGM/cues, other options and
  full submenu animation/localization parity remain outstanding.

- Loading presentation is now a main-thread owner independent of partial world
  construction. Asset-boundary callbacks pump events and submit resident original
  loading art/text; they do not advance Lua or render a partial scene. Preserve
  the title-selected portrait and separate loading item. Current tips use the
  original English column and default Xbox icon mapping. Pinned PNG/zlib internals
  are optimized even in Debug after measured decode cost and pixel-hash equality;
  project Debug validation remains enabled. See `loading-presentation.md`.

- Reusable CPU vector/matrix computation belongs to `bw_math`, with explicit
  scalar and accumulator precision. Keep caller tolerances and validation policy
  local; do not duplicate float/double primitives in subsystem files. Target
  specialization belongs behind the shared numerical contract. See
  `research/engine/subsystems/shared-math.md`.

- In-mission costume selection is an intentional baseline improvement approved
  by the user. Pause owns UI input and freezes session/mission, actor, camera,
  secondary and material updates. Appearance replacement prepares an isolated
  stable-address mission body, validates/prepares actor state, publishes only
  replacement body draws, then retires prior resources. Preserve movement,
  camera and action clocks; do not persist this session choice implicitly.
  The pause menu exposes Resume, Costume, Weapon Class and Animation Review
  with existing authored typography and costume previews. All six costumes pass actual-content native replacement/resume
  checks with preserved frozen body/camera and balanced teardown. Texture
  retirement now invalidates only affected models, preserving the new scene.
  See `pause-and-costume-switching.md` and `model-draw-replacement.md`;
  physical-controller review confirms the menu works. Dress/body intersections
  and backward-run evade facing remain open original-game comparisons.
