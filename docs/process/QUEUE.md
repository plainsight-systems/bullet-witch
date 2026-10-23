# Remaining work

- Next milestone: reach the launch screen and its launch cinematics. Reconstruct advertise using the shared playback/native output already in
  place, then title initialization, character-load save/dialog behavior and
  title-menu status contracts. Follow
  `research/engine/subsystems/startup-next-contracts.md`. Implement required
  sprite/font presentation through renderer-owned contracts; keep the current
  startup check's missing visual presentation explicit. Resolve logo open-failure
  continuation and startup-wide initial fade state from evidence.
- After the launch-screen milestone: implement the memory contracts in
  `docs/architecture/memory-and-residency.md`, including backing, domains/budgets,
  alignment/failure, persistent storage, phase arenas and streaming leases.
  Migrate existing resource, task/Lua and media allocations explicitly; document
  externally managed codec/native overhead.
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
- Revisit the ASan startup limitation when the host/toolchain changes. Keep
  native/UBSan results separate from unavailable ASan execution.
- Establish platform-specific verification when those backends are implemented;
  do not infer Xbox or Windows support from a passing Mac build.
