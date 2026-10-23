# Remaining work

- Complete remaining front-end fidelity: wire persisted volume settings into the
  audio owner, add menu BGM/confirmation cues, reconstruct other options and
  submenu animations/localization. Keep these gaps separate from the delivered
  profile/menu/campaign-request slice. Resolve logo open-failure continuation,
  ordinary media-open rejection versus terminal decoder errors, and startup-wide
  initial fade state at their owning systems.
- Resolve extra-mission native status 5 versus original root status 6 before
  claiming a concept mission handoff. Supply achievement/progression state for
  IDs 9/10/13; the current native app uses fresh locked state. Installed content
  flags alone must not unlock progression gates.
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
- Close camera/view, scene/visibility, lighting/pass, all-mode schedule and
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
