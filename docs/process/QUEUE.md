# Remaining work

- Use the selected One Shot Kill target (`cm01_00`, extra ID 1, stage `st04_c`)
  for SRC-03. Follow the planned breakdown in `IMPLEMENTATION.md`: concept
  launch/mission ownership, required content loading, first static stage view,
  stage presentation, Alicia traversal, mission behavior and full outcome loop.
  Census actual stage/placement dependencies; do not infer an enemy-free level
  from the short Lua script. Resolve PhysX 2.6.4/cooked-collision treatment before
  traversal, without blocking static presentation. Each slice includes its own
  failure/cancellation/release paths and architecture updates.
- Complete remaining front-end fidelity: wire persisted volume settings into the
  audio owner, add menu BGM/confirmation cues, reconstruct other options and
  submenu animations/localization. Keep these gaps separate from the delivered
  profile/menu/campaign-request slice. Resolve logo open-failure continuation,
  ordinary media-open rejection versus terminal decoder errors, and startup-wide
  initial fade state at their owning systems.
- Continue One Shot Kill after its original load now returns: implement actor
  and world setup before calling gameplay start. Loading-scene animated
  sprites/text, authored environment/visibility, background placement and
  complete stage passes remain presentation work. Stage collision/area/material
  animation, character motion/shape definitions and sound-bank registration have
  real load owners; runtime simulation, animation and cue playback are separate
  consumers. Slices 1–3 were accepted for commit on October 23, 2026, after native verification of the
  recovered initial camera, original static clearance, reverse-depth projection,
  authored stage ambient SH and camera-centered sky. Carry the remaining three
  untextured below-stage quads, generated mip chains, visibility and shadow/pass
  behavior into the proposed slice 4, whose implementation awaits agreement;
  do not confuse a coherent static view with complete
  original presentation. The view supplies an explicit recovered script-pose bootstrap;
  later actor start must become its live pose owner. Camera input, animated
  anchor transforms, follow transitions and dynamic collision remain separate. Native internal menu 5 to API 6 and selected concept alias are
  resolved; keep original Lua unchanged. Supply achievement/progression
  state for IDs 9/10/13 for campaign Stage Select and higher difficulties; the
  concept-menu completion gate alone is intentionally removed.
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
- Extend the bounded initial game camera and static spatial query into live
  actor camera/follow and dynamic-scene ownership; preserve the source clearance
  contract and measured query cost when replacing linear broad-phase traversal.
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
